/*
 * td_mqtt.c - MQTT 3.1.1 client over non-blocking TCP.
 */
#include "td_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "td_sock.h"
#include "td_tls.h"

#define RX_CAP 1024
#define TX_CAP 1024
#define CONNECT_TIMEOUT_MS 10000
#define RETRY_MS 5000

typedef struct {
    char topic[TD_MQTT_TOPIC_MAX];
    uint8_t qos;
    bool used;
} sub_t;

typedef struct {
    td_mqtt_config_t cfg;
    td_addr_t addr;
    td_sock_t sock;
    td_tls_t *tls;
    char *ca, *cert, *key;    /* certificate files, kept for reconnects */
    size_t ca_len, cert_len, key_len;
    char security[72];
    td_mqtt_state_t state;
    uint32_t state_ms;        /* when the state was entered */
    uint32_t last_tx_ms;
    uint32_t ping_ms;
    bool ping_out;
    uint16_t next_id;

    uint8_t rx[RX_CAP];
    int rx_len;
    uint32_t skip;            /* bytes of an oversized packet still to drop */

    uint8_t tx[TX_CAP];
    int tx_len;

    sub_t subs[TD_MQTT_SUBS];
    td_mqtt_msg_t log[TD_MQTT_LOG];
    uint32_t seq;
    uint32_t rx_count, tx_count;
    uint32_t connected_ms;
    char text[80];
    char broker[72];
} mqtt_t;

static mqtt_t *M;

/* ------------------------------------------------------------ helpers */

int td_mqtt_encode_length(uint32_t len, uint8_t out[4])
{
    int n = 0;
    do {
        uint8_t b = (uint8_t)(len % 128);
        len /= 128;
        if (len) b |= 0x80;
        out[n++] = b;
    } while (len && n < 4);
    return n;
}

int td_mqtt_decode_length(const uint8_t *p, int avail, uint32_t *len)
{
    uint32_t v = 0, mul = 1;
    for (int i = 0; i < 4; i++) {
        if (i >= avail) return 0;
        v += (uint32_t)(p[i] & 0x7F) * mul;
        if (!(p[i] & 0x80)) {
            *len = v;
            return i + 1;
        }
        mul *= 128;
    }
    return -1;
}

bool td_mqtt_topic_matches(const char *f, const char *t)
{
    if (t[0] == '$' && (f[0] == '#' || f[0] == '+')) return false;   /* $SYS rule */
    while (*f) {
        if (*f == '#') return true;
        if (*f == '+') {
            while (*t && *t != '/') t++;
            f++;
        } else {
            if (*f != *t) return false;
            f++;
            t++;
        }
        if (!*f && !*t) return true;
        if (*f == '/' && f[1] == '#' && !f[2] && !*t) return true;  /* "a/#" matches "a" */
    }
    return !*t;
}

static bool valid_topic(const char *topic, bool filter)
{
    size_t n = topic ? strlen(topic) : 0;
    if (n == 0 || n >= TD_MQTT_TOPIC_MAX) return false;
    if (!filter && (strchr(topic, '+') || strchr(topic, '#'))) return false;
    return true;
}

static void say(const char *fmt, const char *arg)
{
    snprintf(M->text, sizeof(M->text), fmt, arg);
}

static void close_sock(void)
{
    td_tls_free(M->tls);                  /* before the socket it talks over */
    M->tls = NULL;
    M->security[0] = '\0';
    if (M->sock != TD_SOCK_INVALID) td_sock_close(M->sock);
    M->sock = TD_SOCK_INVALID;
    M->rx_len = 0;
    M->tx_len = 0;
    M->skip = 0;
    M->ping_out = false;
}

static void set_state(td_mqtt_state_t s)
{
    M->state = s;
    M->state_ms = td_proto_millis();
}

/* The connection dropped: retry later or stop. */
static void lost(const char *why)
{
    close_sock();
    if (M->cfg.auto_reconnect) {
        snprintf(M->text, sizeof(M->text), "%.56s; retrying in 5 s", why);
        set_state(TD_MQTT_RETRY);
    } else {
        say("%s", why);
        set_state(TD_MQTT_OFF);
    }
}

static void log_msg(bool outgoing, const char *topic, int tlen, const uint8_t *payload, int plen, int qos, bool retain)
{
    td_mqtt_msg_t *m = &M->log[M->seq % TD_MQTT_LOG];
    memset(m, 0, sizeof(*m));
    m->seq = ++M->seq;
    m->ms = td_proto_millis();
    time_t now = time(NULL);
    m->utc = now > 1700000000 ? (int64_t)now : 0;   /* fixed once, so the shown time never wobbles */
    m->outgoing = outgoing;
    m->qos = (uint8_t)qos;
    m->retain = retain;
    m->len = (uint16_t)(plen > 65535 ? 65535 : plen);
    int tn = tlen < TD_MQTT_TOPIC_MAX - 1 ? tlen : TD_MQTT_TOPIC_MAX - 1;
    memcpy(m->topic, topic, (size_t)tn);
    int pn = plen < TD_MQTT_PAYLOAD_KEEP ? plen : TD_MQTT_PAYLOAD_KEEP;
    memcpy(m->payload, payload, (size_t)pn);
}

/* ------------------------------------------------------------ sending */

static int io_send(const void *buf, int len)
{
    return M->tls ? td_tls_send(M->tls, buf, len) : td_sock_send(M->sock, buf, len);
}

static int io_recv(void *buf, int cap)
{
    return M->tls ? td_tls_recv(M->tls, buf, cap) : td_sock_recv(M->sock, buf, cap);
}

static bool flush_tx(void)
{
    if (M->tx_len == 0 || M->sock == TD_SOCK_INVALID) return true;
    if (M->state == TD_MQTT_CONNECTING || M->state == TD_MQTT_TLS_HANDSHAKE) return true;
    int n = io_send(M->tx, M->tx_len);
    if (n < 0) {
        lost("Connection lost while sending");
        return false;
    }
    if (n > 0) {
        memmove(M->tx, M->tx + n, (size_t)(M->tx_len - n));
        M->tx_len -= n;
        M->last_tx_ms = td_proto_millis();
    }
    return true;
}

/* Queue one packet: header byte, remaining length, then the parts. */
static bool queue_packet(uint8_t header, const uint8_t *a, int alen, const uint8_t *b, int blen)
{
    uint8_t len[4];
    int ln = td_mqtt_encode_length((uint32_t)(alen + blen), len);
    int total = 1 + ln + alen + blen;
    if (M->tx_len + total > TX_CAP) return false;
    uint8_t *p = M->tx + M->tx_len;
    *p++ = header;
    memcpy(p, len, (size_t)ln);
    p += ln;
    if (alen) memcpy(p, a, (size_t)alen);
    p += alen;
    if (blen) memcpy(p, b, (size_t)blen);
    M->tx_len += total;
    return flush_tx();
}

static int put_str(uint8_t *p, const char *s)
{
    size_t n = strlen(s);
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
    memcpy(p + 2, s, n);
    return (int)n + 2;
}

static uint16_t new_id(void)
{
    if (++M->next_id == 0) M->next_id = 1;
    return M->next_id;
}

static void send_connect(void)
{
    uint8_t v[420];
    int n = 0;
    n += put_str(v + n, "MQTT");
    v[n++] = 4;                                   /* protocol level 3.1.1 */
    uint8_t flags = M->cfg.persistent ? 0 : 0x02; /* clean session */
    if (M->cfg.user[0]) flags |= 0x80;
    if (M->cfg.user[0] && M->cfg.pass[0]) flags |= 0x40;
    if (M->cfg.will_topic[0]) {
        flags |= 0x04;
        flags |= (uint8_t)((M->cfg.will_qos > 1 ? 1 : M->cfg.will_qos) << 3);
        if (M->cfg.will_retain) flags |= 0x20;
    }
    v[n++] = flags;
    v[n++] = (uint8_t)(M->cfg.keepalive >> 8);
    v[n++] = (uint8_t)M->cfg.keepalive;
    n += put_str(v + n, M->cfg.client_id);
    if (flags & 0x04) {
        n += put_str(v + n, M->cfg.will_topic);
        n += put_str(v + n, M->cfg.will_payload);
    }
    if (flags & 0x80) n += put_str(v + n, M->cfg.user);
    if (flags & 0x40) n += put_str(v + n, M->cfg.pass);
    queue_packet(0x10, v, n, NULL, 0);
    memset(v, 0, sizeof(v));                      /* the password was in here */
}

static bool send_subscribe(const char *topic, int qos)
{
    uint8_t v[2 + 2 + TD_MQTT_TOPIC_MAX + 1];
    uint16_t id = new_id();
    int n = 0;
    v[n++] = (uint8_t)(id >> 8);
    v[n++] = (uint8_t)id;
    n += put_str(v + n, topic);
    v[n++] = (uint8_t)qos;
    return queue_packet(0x82, v, n, NULL, 0);
}

static bool send_unsubscribe(const char *topic)
{
    uint8_t v[2 + 2 + TD_MQTT_TOPIC_MAX];
    uint16_t id = new_id();
    int n = 0;
    v[n++] = (uint8_t)(id >> 8);
    v[n++] = (uint8_t)id;
    n += put_str(v + n, topic);
    return queue_packet(0xA2, v, n, NULL, 0);
}

/* ------------------------------------------------------------ receiving */

static const char *connack_text(int rc)
{
    switch (rc) {
    case 1: return "Refused: unsupported protocol version";
    case 2: return "Refused: client id rejected";
    case 3: return "Refused: server unavailable";
    case 4: return "Refused: bad user name or password";
    case 5: return "Refused: not authorised";
    default: return "Refused by the broker";
    }
}

static void handle_packet(uint8_t hdr, const uint8_t *p, uint32_t len)
{
    uint8_t type = hdr >> 4;
    switch (type) {
    case 2:                                                   /* CONNACK */
        if (len >= 2 && p[1] == 0) {
            set_state(TD_MQTT_CONNECTED);
            M->connected_ms = td_proto_millis();
            snprintf(M->text, sizeof(M->text), "Connected to %.60s%s", M->broker, M->tls ? " (TLS)" : "");
            for (int i = 0; i < TD_MQTT_SUBS; i++)
                if (M->subs[i].used) send_subscribe(M->subs[i].topic, M->subs[i].qos);
        } else {
            M->cfg.auto_reconnect = false;   /* retrying will not help */
            lost(connack_text(len >= 2 ? p[1] : -1));
        }
        break;
    case 3: {                                                 /* PUBLISH */
        int qos = (hdr >> 1) & 3;
        if (len < 2) return;
        int tlen = (p[0] << 8) | p[1];
        uint32_t at = 2 + (uint32_t)tlen;
        if (at > len) return;
        uint16_t id = 0;
        if (qos > 0) {
            if (at + 2 > len) return;
            id = (uint16_t)((p[at] << 8) | p[at + 1]);
            at += 2;
        }
        log_msg(false, (const char *)p + 2, tlen, p + at, (int)(len - at), qos, (hdr & 1) != 0);
        M->rx_count++;
        if (qos == 1) {
            uint8_t ack[2] = { (uint8_t)(id >> 8), (uint8_t)id };
            queue_packet(0x40, ack, 2, NULL, 0);
        } else if (qos == 2) {                                /* PUBREC */
            uint8_t ack[2] = { (uint8_t)(id >> 8), (uint8_t)id };
            queue_packet(0x50, ack, 2, NULL, 0);
        }
        break;
    }
    case 6: {                                                 /* PUBREL -> PUBCOMP */
        if (len >= 2) queue_packet(0x70, p, 2, NULL, 0);
        break;
    }
    case 9:                                                   /* SUBACK */
        if (len >= 3 && p[2] == 0x80) say("%s", "The broker refused a subscription");
        break;
    case 13:                                                  /* PINGRESP */
        M->ping_out = false;
        break;
    default:                                                  /* PUBACK, UNSUBACK... */
        break;
    }
}

static void read_socket(void)
{
    for (int guard = 0; guard < 8 && M->sock != TD_SOCK_INVALID; guard++) {
        if (M->skip) {                          /* drop the rest of a huge packet */
            uint8_t junk[128];
            int want = M->skip < sizeof(junk) ? (int)M->skip : (int)sizeof(junk);
            int n = io_recv(junk, want);
            if (n < 0) {
                lost("Connection closed by the broker");
                return;
            }
            if (n == 0) return;
            M->skip -= (uint32_t)n;
            continue;
        }
        int n = io_recv(M->rx + M->rx_len, RX_CAP - M->rx_len);
        if (n < 0) {
            lost("Connection closed by the broker");
            return;
        }
        if (n == 0) break;
        M->rx_len += n;

        /* Handle every complete packet in the buffer. */
        for (;;) {
            if (M->rx_len < 2) break;
            uint32_t len;
            int ln = td_mqtt_decode_length(M->rx + 1, M->rx_len - 1, &len);
            if (ln < 0) {
                lost("Bad data from the broker");
                return;
            }
            if (ln == 0) break;
            uint32_t total = 1 + (uint32_t)ln + len;
            if (total > RX_CAP) {               /* too big to keep: skip it */
                M->skip = total - (uint32_t)M->rx_len;
                M->rx_len = 0;
                break;
            }
            if ((uint32_t)M->rx_len < total) break;
            handle_packet(M->rx[0], M->rx + 1 + ln, len);
            if (M->sock == TD_SOCK_INVALID) return;
            memmove(M->rx, M->rx + total, (size_t)M->rx_len - total);
            M->rx_len -= (int)total;
        }
    }
}

/* ------------------------------------------------------------- driving */

static void start_tcp(void)
{
    char err[64];
    M->sock = td_sock_connect_start(&M->addr, err, sizeof(err));
    if (M->sock == TD_SOCK_INVALID) {
        lost(err);
        return;
    }
    set_state(TD_MQTT_CONNECTING);
    snprintf(M->text, sizeof(M->text), "Connecting to %.60s...", M->broker);
}

static void poll_locked(void)
{
    if (!M) return;
    uint32_t now = td_proto_millis();
    switch (M->state) {
    case TD_MQTT_OFF:
        return;
    case TD_MQTT_RETRY:
        if (now - M->state_ms >= RETRY_MS) start_tcp();
        return;
    case TD_MQTT_CONNECTING: {
        char err[64] = "Connection failed";
        int r = td_sock_connect_poll(M->sock, err, sizeof(err));
        if (r < 0) {
            lost(err);
        } else if (r > 0) {
            if (M->cfg.tls) {
                td_tls_config_t tc = {
                    .ca = M->ca, .ca_len = M->ca_len,
                    .cert = M->cert, .cert_len = M->cert_len,
                    .key = M->key, .key_len = M->key_len,
                    .key_pass = M->cfg.key_pass,
                    .server_name = M->cfg.host,
                    .insecure = M->cfg.insecure,
                };
                char e2[96];
                M->tls = td_tls_start(M->sock, &tc, e2, sizeof(e2));
                if (!M->tls) {
                    M->cfg.auto_reconnect = false;     /* a setup error will not fix itself */
                    lost(e2);
                    return;
                }
                set_state(TD_MQTT_TLS_HANDSHAKE);
                snprintf(M->text, sizeof(M->text), "TLS handshake with %.56s...", M->broker);
            } else {
                set_state(TD_MQTT_WAIT_CONNACK);
                send_connect();
            }
        } else if (now - M->state_ms > CONNECT_TIMEOUT_MS) {
            lost("No answer from the broker (timeout)");
        }
        return;
    }
    case TD_MQTT_TLS_HANDSHAKE: {
        char e2[160];
        int r = td_tls_handshake(M->tls, e2, sizeof(e2));
        if (r < 0) {
            if (strstr(e2, "rejected")) M->cfg.auto_reconnect = false;   /* bad certificate */
            lost(e2);
        } else if (r > 0) {
            td_tls_info(M->tls, M->security, sizeof(M->security));
            set_state(TD_MQTT_WAIT_CONNACK);
            send_connect();
        } else if (now - M->state_ms > CONNECT_TIMEOUT_MS) {
            lost("TLS handshake timed out");
        }
        return;
    }
    case TD_MQTT_WAIT_CONNACK:
        if (now - M->state_ms > CONNECT_TIMEOUT_MS) {
            lost("The broker did not accept the connection (timeout)");
            return;
        }
        break;
    case TD_MQTT_CONNECTED: {
        uint32_t ka = (uint32_t)M->cfg.keepalive * 1000u;
        if (M->ping_out && now - M->ping_ms > ka / 2 + 5000) {
            lost("The broker stopped answering");
            return;
        }
        if (!M->ping_out && now - M->last_tx_ms >= ka * 3 / 4) {
            if (queue_packet(0xC0, NULL, 0, NULL, 0)) {
                M->ping_out = true;
                M->ping_ms = now;
            }
            if (M->sock == TD_SOCK_INVALID) return;
        }
        break;
    }
    }
    if (!flush_tx()) return;
    read_socket();
}

void td_mqtt_poll(void)
{
    if (!M) return;                              /* cheap when unused */
    td_proto_lock();
    poll_locked();
    td_proto_unlock();
}

/* ------------------------------------------------------------- the API */

static void free_files(mqtt_t *m)
{
    if (m->key) memset(m->key, 0, m->key_len);     /* private key material */
    free(m->ca);
    free(m->cert);
    free(m->key);
    m->ca = m->cert = m->key = NULL;
    m->ca_len = m->cert_len = m->key_len = 0;
}

bool td_mqtt_connect(const td_mqtt_config_t *cfg, char *err, size_t cap)
{
    td_addr_t addr;
    uint16_t port = cfg->port ? cfg->port : cfg->tls ? TD_MQTT_TLS_PORT : TD_MQTT_DEFAULT_PORT;
    if (!cfg->host[0]) {
        snprintf(err, cap, "No broker given");
        return false;
    }
    if (cfg->tls && !td_tls_available()) {
        snprintf(err, cap, "TLS (mqtts) is not available in this build");
        return false;
    }
    /* Certificate files are read now, outside the lock. */
    char *ca = NULL, *cert = NULL, *key = NULL;
    size_t ca_len = 0, cert_len = 0, key_len = 0;
    if (cfg->tls) {
        if ((cfg->ca_file[0] && !(ca = td_read_file(cfg->ca_file, TD_MQTT_CERT_MAX, &ca_len, err, cap))) ||
            (cfg->cert_file[0] && !(cert = td_read_file(cfg->cert_file, TD_MQTT_CERT_MAX, &cert_len, err, cap))) ||
            (cfg->key_file[0] && !(key = td_read_file(cfg->key_file, TD_MQTT_CERT_MAX, &key_len, err, cap)))) {
            free(ca);
            free(cert);
            free(key);
            return false;
        }
        if (!cert != !key) {
            free(ca);
            free(cert);
            free(key);
            snprintf(err, cap, "certfile and keyfile go together");
            return false;
        }
    }
    if (!td_sock_resolve(cfg->host, port, &addr, err, cap)) {    /* DNS: unlocked */
        free(ca);
        free(cert);
        if (key) memset(key, 0, key_len);
        free(key);
        return false;
    }

    td_proto_lock();
    if (!M) {
        M = calloc(1, sizeof(*M));
        if (!M) {
            td_proto_unlock();
            snprintf(err, cap, "Not enough memory");
            return false;
        }
        M->sock = TD_SOCK_INVALID;
    } else {
        close_sock();
        free_files(M);
        memset(M->log, 0, sizeof(M->log));
    }
    M->ca = ca;
    M->cert = cert;
    M->key = key;
    M->ca_len = ca_len;
    M->cert_len = cert_len;
    M->key_len = key_len;
    M->cfg = *cfg;
    M->cfg.port = port;
    for (int i = 0; i < cfg->nsubs && i < TD_MQTT_SUBS; i++) {   /* subscribe on connect */
        bool dup = false;
        for (int k = 0; k < TD_MQTT_SUBS; k++)
            if (M->subs[k].used && strcmp(M->subs[k].topic, cfg->sub_topic[i]) == 0) dup = true;
        for (int k = 0; k < TD_MQTT_SUBS && !dup; k++) {
            if (M->subs[k].used) continue;
            snprintf(M->subs[k].topic, sizeof(M->subs[k].topic), "%s", cfg->sub_topic[i]);
            M->subs[k].qos = cfg->sub_qos[i] > 1 ? 1 : cfg->sub_qos[i];
            M->subs[k].used = true;
            break;
        }
    }
    if (!M->cfg.keepalive) M->cfg.keepalive = 60;
    if (!M->cfg.client_id[0])
        snprintf(M->cfg.client_id, sizeof(M->cfg.client_id), "tinydesk-%04x%04x",
                 (unsigned)(td_proto_millis() & 0xFFFF), (unsigned)((uintptr_t)M & 0xFFFF));
    M->addr = addr;
    M->seq = 0;
    M->rx_count = M->tx_count = 0;
    snprintf(M->broker, sizeof(M->broker), "%.60s:%u", cfg->host, (unsigned)port);
    start_tcp();
    td_proto_unlock();
    return true;
}

void td_mqtt_disconnect(void)
{
    td_proto_lock();
    if (M) {
        if (M->state == TD_MQTT_CONNECTED) {
            queue_packet(0xE0, NULL, 0, NULL, 0);
            flush_tx();
        }
        close_sock();
        free_files(M);
        memset(M, 0, sizeof(*M));                 /* credentials too */
        free(M);
        M = NULL;
    }
    td_proto_unlock();
}

void td_mqtt_status(td_mqtt_status_t *out)
{
    memset(out, 0, sizeof(*out));
    td_proto_lock();
    if (M) {
        out->state = M->state;
        snprintf(out->text, sizeof(out->text), "%s", M->text);
        snprintf(out->broker, sizeof(out->broker), "%s", M->broker);
        out->rx = M->rx_count;
        out->tx = M->tx_count;
        out->connected_ms = M->connected_ms;
        out->tls = M->cfg.tls;
        snprintf(out->security, sizeof(out->security), "%s", M->security);
        for (int i = 0; i < TD_MQTT_SUBS; i++) out->subs += M->subs[i].used;
    } else {
        snprintf(out->text, sizeof(out->text), "Not connected");
    }
    td_proto_unlock();
}

int td_mqtt_subscribe(const char *topic, int qos)
{
    if (!valid_topic(topic, true)) return -1;
    if (qos < 0) qos = 0;
    if (qos > 1) qos = 1;
    int rc = -1;
    td_proto_lock();
    if (M) {
        int free_slot = -1, found = -1;
        for (int i = 0; i < TD_MQTT_SUBS; i++) {
            if (M->subs[i].used && strcmp(M->subs[i].topic, topic) == 0) found = i;
            if (!M->subs[i].used && free_slot < 0) free_slot = i;
        }
        int slot = found >= 0 ? found : free_slot;
        if (slot < 0) {
            rc = -2;
        } else {
            snprintf(M->subs[slot].topic, sizeof(M->subs[slot].topic), "%s", topic);
            M->subs[slot].qos = (uint8_t)qos;
            M->subs[slot].used = true;
            if (M->state == TD_MQTT_CONNECTED) send_subscribe(topic, qos);
            rc = 0;
        }
    }
    td_proto_unlock();
    return rc;
}

int td_mqtt_unsubscribe(const char *topic)
{
    int rc = -1;
    td_proto_lock();
    if (M) {
        for (int i = 0; i < TD_MQTT_SUBS; i++) {
            if (!M->subs[i].used || strcmp(M->subs[i].topic, topic) != 0) continue;
            M->subs[i].used = false;
            if (M->state == TD_MQTT_CONNECTED) send_unsubscribe(topic);
            rc = 0;
        }
    }
    td_proto_unlock();
    return rc;
}

bool td_mqtt_subscription(int i, char *topic, size_t cap, int *qos)
{
    bool r = false;
    td_proto_lock();
    if (M) {
        int n = 0;
        for (int k = 0; k < TD_MQTT_SUBS; k++) {
            if (!M->subs[k].used) continue;
            if (n++ == i) {
                snprintf(topic, cap, "%s", M->subs[k].topic);
                if (qos) *qos = M->subs[k].qos;
                r = true;
                break;
            }
        }
    }
    td_proto_unlock();
    return r;
}

int td_mqtt_publish(const char *topic, const void *payload, int len, int qos, bool retain)
{
    if (!valid_topic(topic, false) || len < 0) return -1;
    if (qos < 0) qos = 0;
    if (qos > 1) qos = 1;
    int rc = -1;
    td_proto_lock();
    if (M && M->state == TD_MQTT_CONNECTED) {
        uint8_t v[2 + TD_MQTT_TOPIC_MAX + 2];
        int n = put_str(v, topic);
        if (qos) {
            uint16_t id = new_id();
            v[n++] = (uint8_t)(id >> 8);
            v[n++] = (uint8_t)id;
        }
        uint8_t hdr = (uint8_t)(0x30 | (qos << 1) | (retain ? 1 : 0));
        if (queue_packet(hdr, v, n, (const uint8_t *)payload, len)) {
            log_msg(true, topic, (int)strlen(topic), (const uint8_t *)payload, len, qos, retain);
            M->tx_count++;
            rc = 0;
        } else {
            rc = M->sock == TD_SOCK_INVALID ? -1 : -2;
        }
    }
    td_proto_unlock();
    return rc;
}

uint32_t td_mqtt_last_seq(void)
{
    td_proto_lock();
    uint32_t s = M ? M->seq : 0;
    td_proto_unlock();
    return s;
}

bool td_mqtt_message(uint32_t seq, td_mqtt_msg_t *out)
{
    bool ok = false;
    td_proto_lock();
    if (M && seq > 0 && seq <= M->seq && M->seq - seq < TD_MQTT_LOG) {
        const td_mqtt_msg_t *m = &M->log[(seq - 1) % TD_MQTT_LOG];
        if (m->seq == seq) {
            *out = *m;
            ok = true;
        }
    }
    td_proto_unlock();
    return ok;
}
