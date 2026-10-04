/*
 * td_modbus.c - Modbus TCP/RTU client and Modbus TCP server.
 */
#include "td_modbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_sock.h"

#define TCP_TIMEOUT_MS 2000
#define RTU_TIMEOUT_MS 1000
#define IDLE_CLOSE_MS  15000       /* close an unused TCP link / serial port */
#define SERVER_CLIENTS 3
#define FRAME_MAX      260

/* ---------------------------------------------------------- pure parts */

uint16_t td_mb_crc16(const uint8_t *p, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

bool td_mb_parse_table(const char *n, td_mb_table_t *out)
{
    if (!strcmp(n, "co") || !strcmp(n, "coil") || !strcmp(n, "coils") || !strcmp(n, "0x"))
        *out = TD_MB_COILS;
    else if (!strcmp(n, "di") || !strcmp(n, "discrete") || !strcmp(n, "inputs") || !strcmp(n, "1x"))
        *out = TD_MB_DISCRETE;
    else if (!strcmp(n, "hr") || !strcmp(n, "holding") || !strcmp(n, "4x"))
        *out = TD_MB_HOLDING;
    else if (!strcmp(n, "ir") || !strcmp(n, "input") || !strcmp(n, "3x"))
        *out = TD_MB_INPUT;
    else
        return false;
    return true;
}

const char *td_mb_table_name(td_mb_table_t t)
{
    switch (t)
    {
    case TD_MB_COILS:
        return "coils";
    case TD_MB_DISCRETE:
        return "discrete inputs";
    case TD_MB_HOLDING:
        return "holding registers";
    case TD_MB_INPUT:
        return "input registers";
    default:
        return "?";
    }
}

const char *td_mb_exception_text(int code)
{
    switch (code)
    {
    case 1:
        return "illegal function";
    case 2:
        return "illegal data address";
    case 3:
        return "illegal data value";
    case 4:
        return "server device failure";
    case 5:
        return "acknowledge (busy, try later)";
    case 6:
        return "server device busy";
    case 10:
        return "gateway path unavailable";
    case 11:
        return "gateway target did not respond";
    default:
        return "exception";
    }
}

static bool get_bit(const uint8_t *bits, int i)
{
    return (bits[i / 8] >> (i % 8)) & 1;
}

static void set_bit(uint8_t *bits, int i, bool on)
{
    if (on)
        bits[i / 8] |= (uint8_t)(1u << (i % 8));
    else
        bits[i / 8] &= (uint8_t) ~(1u << (i % 8));
}

static int exception(uint8_t fc, uint8_t code, uint8_t *resp)
{
    resp[0] = (uint8_t)(fc | 0x80);
    resp[1] = code;
    return 2;
}

int td_mb_serve_pdu(td_mb_tables_t *t, const uint8_t *req, int len, uint8_t *resp)
{
    if (len < 1)
        return 0;
    uint8_t fc = req[0];
    if (len < 5)
        return exception(fc, 3, resp);
    uint16_t addr = be16(req + 1), qty = be16(req + 3);
    switch (fc)
    {
    case 1:
    case 2:
    {
        if (qty < 1 || qty > 2000)
            return exception(fc, 3, resp);
        if ((uint32_t)addr + qty > TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        const uint8_t *bits = fc == 1 ? t->coils : t->discrete;
        int nbytes = (qty + 7) / 8;
        resp[0] = fc;
        resp[1] = (uint8_t)nbytes;
        memset(resp + 2, 0, (size_t)nbytes);
        for (int i = 0; i < qty; i++)
            if (get_bit(bits, addr + i))
                resp[2 + i / 8] |= (uint8_t)(1u << (i % 8));
        return 2 + nbytes;
    }
    case 3:
    case 4:
    {
        if (qty < 1 || qty > 125)
            return exception(fc, 3, resp);
        if ((uint32_t)addr + qty > TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        const uint16_t *regs = fc == 3 ? t->holding : t->input;
        resp[0] = fc;
        resp[1] = (uint8_t)(qty * 2);
        for (int i = 0; i < qty; i++)
            put16(resp + 2 + i * 2, regs[addr + i]);
        return 2 + qty * 2;
    }
    case 5:
        if (addr >= TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        if (qty != 0xFF00 && qty != 0x0000)
            return exception(fc, 3, resp);
        set_bit(t->coils, addr, qty == 0xFF00);
        memcpy(resp, req, 5);
        return 5;
    case 6:
        if (addr >= TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        t->holding[addr] = qty;
        memcpy(resp, req, 5);
        return 5;
    case 15:
    {
        if (len < 6 || qty < 1 || qty > 1968 || req[5] != (qty + 7) / 8 || len < 6 + req[5])
            return exception(fc, 3, resp);
        if ((uint32_t)addr + qty > TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        for (int i = 0; i < qty; i++)
            set_bit(t->coils, addr + i, (req[6 + i / 8] >> (i % 8)) & 1);
        memcpy(resp, req, 5);
        return 5;
    }
    case 16:
    {
        if (len < 6 || qty < 1 || qty > 123 || req[5] != qty * 2 || len < 6 + req[5])
            return exception(fc, 3, resp);
        if ((uint32_t)addr + qty > TD_MB_TABLE_SIZE)
            return exception(fc, 2, resp);
        for (int i = 0; i < qty; i++)
            t->holding[addr + i] = be16(req + 6 + i * 2);
        memcpy(resp, req, 5);
        return 5;
    }
    default:
        return exception(fc, 1, resp);
    }
}

int td_mb_build_pdu(const td_mb_request_t *r, uint8_t *pdu)
{
    pdu[0] = r->fc;
    put16(pdu + 1, r->addr);
    switch (r->fc)
    {
    case 1:
    case 2:
        if (r->count < 1 || r->count > 2000)
            return -1;
        put16(pdu + 3, r->count);
        return 5;
    case 3:
    case 4:
        if (r->count < 1 || r->count > TD_MB_MAX_READ)
            return -1;
        put16(pdu + 3, r->count);
        return 5;
    case 5:
        if (r->nvalues != 1)
            return -1;
        put16(pdu + 3, r->values[0] ? 0xFF00 : 0x0000);
        return 5;
    case 6:
        if (r->nvalues != 1)
            return -1;
        put16(pdu + 3, r->values[0]);
        return 5;
    case 15:
    {
        if (r->nvalues < 1 || r->nvalues > TD_MB_MAX_WRITE)
            return -1;
        int nbytes = (r->nvalues + 7) / 8;
        put16(pdu + 3, r->nvalues);
        pdu[5] = (uint8_t)nbytes;
        memset(pdu + 6, 0, (size_t)nbytes);
        for (int i = 0; i < r->nvalues; i++)
            if (r->values[i])
                pdu[6 + i / 8] |= (uint8_t)(1u << (i % 8));
        return 6 + nbytes;
    }
    case 16:
        if (r->nvalues < 1 || r->nvalues > TD_MB_MAX_WRITE)
            return -1;
        put16(pdu + 3, r->nvalues);
        pdu[5] = (uint8_t)(r->nvalues * 2);
        for (int i = 0; i < r->nvalues; i++)
            put16(pdu + 6 + i * 2, r->values[i]);
        return 6 + r->nvalues * 2;
    default:
        return -1;
    }
}

void td_mb_decode_pdu(const td_mb_request_t *r, const uint8_t *p, int len, td_mb_result_t *res)
{
    res->fc = r->fc;
    res->addr = r->addr;
    res->count = 0;
    if (len >= 2 && p[0] == (r->fc | 0x80))
    {
        res->status = p[1];
        snprintf(res->text, sizeof(res->text), "Exception %d: %s", p[1], td_mb_exception_text(p[1]));
        return;
    }
    if (len < 2 || p[0] != r->fc)
    {
        res->status = -3;
        snprintf(res->text, sizeof(res->text), "Unexpected answer (function %d)", len ? p[0] : -1);
        return;
    }
    switch (r->fc)
    {
    case 1:
    case 2:
    {
        int n = r->count < TD_MB_MAX_READ ? r->count : TD_MB_MAX_READ;
        if (len < 2 + p[1] || p[1] < (r->count + 7) / 8)
            break;
        for (int i = 0; i < n; i++)
            res->values[i] = (p[2 + i / 8] >> (i % 8)) & 1;
        res->count = (uint16_t)n;
        res->status = 0;
        snprintf(res->text, sizeof(res->text), "Read %d %s", n, td_mb_table_name((td_mb_table_t)r->fc));
        return;
    }
    case 3:
    case 4:
    {
        if (len < 2 + p[1] || p[1] != r->count * 2)
            break;
        for (int i = 0; i < r->count; i++)
            res->values[i] = be16(p + 2 + i * 2);
        res->count = r->count;
        res->status = 0;
        snprintf(res->text, sizeof(res->text), "Read %d %s", r->count, td_mb_table_name((td_mb_table_t)r->fc));
        return;
    }
    default:
        if (len >= 5)
        {
            res->status = 0;
            snprintf(res->text, sizeof(res->text), "Wrote %d value%s", r->nvalues, r->nvalues == 1 ? "" : "s");
            return;
        }
        break;
    }
    res->status = -3;
    snprintf(res->text, sizeof(res->text), "Malformed answer");
}

/* RTU: expected response length from its first bytes (0: not known yet). */
static int rtu_expected(const uint8_t *p, int have)
{
    if (have < 2)
        return 0;
    if (p[1] & 0x80)
        return 5;
    switch (p[1])
    {
    case 1:
    case 2:
    case 3:
    case 4:
        return have < 3 ? 0 : 3 + p[2] + 2;
    case 5:
    case 6:
    case 15:
    case 16:
        return 8;
    default:
        return 5;
    }
}

/* ------------------------------------------------------------- client */

typedef enum
{
    C_IDLE,
    C_CONNECTING,
    C_SENDING,
    C_WAITING,
    C_DONE
} cstate_t;

typedef struct
{
    td_mb_request_t req;
    td_mb_result_t res;
    cstate_t state;
    int ticket;
    uint32_t start_ms, last_use_ms;
    bool rtu;

    /* TCP link, kept open while the same target is used. */
    td_sock_t sock;
    td_addr_t addr;
    char link[64];            /* target the socket is connected to */
    uint16_t tid;

    /* RTU line */
    bool serial_open;
    int line;                 /* 1, 2, ... */
    uint32_t baud;
    char parity;

    uint8_t frame[FRAME_MAX];
    int frame_len, sent;
    uint8_t in[FRAME_MAX];
    int in_len;
} client_t;

static client_t *C;
static int s_next_ticket;
static const td_mb_serial_t *s_serial;

void td_mb_set_serial(const td_mb_serial_t *serial)
{
    s_serial = serial;
}
const td_mb_serial_t *td_mb_serial(void)
{
    return s_serial;
}

static void close_link(void)
{
    if (C->sock != TD_SOCK_INVALID)
        td_sock_close(C->sock);
    C->sock = TD_SOCK_INVALID;
    C->link[0] = '\0';
}

static void close_serial(void)
{
    if (C->serial_open && s_serial && s_serial->close)
        s_serial->close();
    C->serial_open = false;
}

static void finish(int status, const char *text)
{
    C->res.status = status;
    if (text)
        snprintf(C->res.text, sizeof(C->res.text), "%s", text);
    C->res.ms = td_proto_millis() - C->start_ms;
    C->state = C_DONE;
    C->last_use_ms = td_proto_millis();
}

/* "rtu", "rtu2", "rtu:19200", "rtu1:9600:8E1" */
static bool parse_rtu(const char *t, int *line, uint32_t *baud, char *parity, char *err, size_t cap)
{
    *line = 1;
    *baud = 9600;
    *parity = 'N';
    if (strncmp(t, "rtu", 3) != 0)
        return false;
    t += 3;
    if (*t >= '1' && *t <= '9')
        *line = *t++ - '0';
    if (*t && *t != ':')
        return false;
    if (*t == ':')
    {
        char *end = NULL;
        long b = strtol(t + 1, &end, 10);
        if (b < 1200 || b > 1000000)
        {
            snprintf(err, cap, "Bad baud rate in %.40s", t);
            return true;
        }
        *baud = (uint32_t)b;
        if (end && *end == ':')
        {
            if (strlen(end + 1) != 3 || end[1] != '8' || end[3] != '1' ||
                (end[2] != 'N' && end[2] != 'E' && end[2] != 'O'))
            {
                snprintf(err, cap, "Frame format must be 8N1, 8E1 or 8O1");
                return true;
            }
            *parity = end[2];
        }
    }
    return true;
}

int td_mb_submit(const td_mb_request_t *req, char *err, size_t cap)
{
    uint8_t pdu[FRAME_MAX];
    int plen = td_mb_build_pdu(req, pdu);
    if (plen < 0)
    {
        snprintf(err, cap, "Bad request (check function, count and values)");
        return 0;
    }
    char e2[72] = "";
    uint32_t baud = 9600;
    char parity = 'N';
    int line = 1;
    bool rtu = parse_rtu(req->target, &line, &baud, &parity, e2, sizeof(e2));
    if (e2[0])
    {
        snprintf(err, cap, "%s", e2);
        return 0;
    }
    if (rtu && !s_serial)
    {
        snprintf(err, cap, "Modbus RTU is not available on this device");
        return 0;
    }
    if (rtu && line > s_serial->ports)
    {
        snprintf(err, cap, "There is no RTU line %d here (rtu1..rtu%d)", line, s_serial->ports);
        return 0;
    }
    char host[64];
    uint16_t port = TD_MB_TCP_PORT;
    td_addr_t addr;
    bool need_resolve = false;
    if (!rtu)
    {
        if (!td_split_host_port(req->target, host, sizeof(host), &port, TD_MB_TCP_PORT))
        {
            snprintf(err, cap, "Target must be host[:port] or rtu[:baud]");
            return 0;
        }
        td_proto_lock();
        need_resolve = !C || strcmp(C->link, req->target) != 0 || C->sock == TD_SOCK_INVALID;
        td_proto_unlock();
        if (need_resolve && !td_sock_resolve(host, port, &addr, err, cap))
            return 0;   /* DNS unlocked */
    }

    td_proto_lock();
    if (!C)
    {
        C = calloc(1, sizeof(*C));
        if (!C)
        {
            td_proto_unlock();
            snprintf(err, cap, "Not enough memory");
            return 0;
        }
        C->sock = TD_SOCK_INVALID;
    }
    if (C->state != C_IDLE && C->state != C_DONE)
    {
        td_proto_unlock();
        snprintf(err, cap, "Busy with another request");
        return 0;
    }
    C->req = *req;
    memset(&C->res, 0, sizeof(C->res));
    C->rtu = rtu;
    C->start_ms = td_proto_millis();
    C->in_len = 0;
    C->sent = 0;
    C->ticket = ++s_next_ticket;
    if (s_next_ticket > 1000000)
        s_next_ticket = 0;

    if (rtu)
    {
        if (C->sock != TD_SOCK_INVALID)
            close_link();
        if (C->serial_open && (C->line != line || C->baud != baud || C->parity != parity))
            close_serial();
        if (!C->serial_open)
        {
            if (!s_serial->open(line, baud, parity, 1, e2, sizeof(e2)))
            {
                C->state = C_IDLE;
                td_proto_unlock();
                snprintf(err, cap, "%s", e2[0] ? e2 : "Cannot open the serial port");
                return 0;
            }
            C->serial_open = true;
            C->line = line;
            C->baud = baud;
            C->parity = parity;
        }
        C->frame[0] = req->unit;
        memcpy(C->frame + 1, pdu, (size_t)plen);
        uint16_t crc = td_mb_crc16(C->frame, plen + 1);
        C->frame[plen + 1] = (uint8_t)crc;          /* CRC is little-endian */
        C->frame[plen + 2] = (uint8_t)(crc >> 8);
        C->frame_len = plen + 3;
        uint8_t junk[64];
        while (s_serial->read(junk, sizeof(junk)) > 0)
        {
        }   /* drop stale bytes */
        C->state = C_SENDING;
    }
    else
    {
        close_serial();
        if (need_resolve)
        {
            close_link();
            C->addr = addr;
            C->sock = td_sock_connect_start(&C->addr, e2, sizeof(e2));
            if (C->sock == TD_SOCK_INVALID)
            {
                C->state = C_IDLE;
                td_proto_unlock();
                snprintf(err, cap, "%s", e2);
                return 0;
            }
            snprintf(C->link, sizeof(C->link), "%s", req->target);
            C->state = C_CONNECTING;
        }
        else
        {
            C->state = C_SENDING;
        }
        C->tid++;
        put16(C->frame, C->tid);
        put16(C->frame + 2, 0);
        put16(C->frame + 4, (uint16_t)(plen + 1));
        C->frame[6] = req->unit;
        memcpy(C->frame + 7, pdu, (size_t)plen);
        C->frame_len = plen + 7;
    }
    int t = C->ticket;
    td_proto_unlock();
    return t;
}

static void client_poll(void)
{
    if (!C)
        return;
    uint32_t now = td_proto_millis();
    if (C->state == C_IDLE || C->state == C_DONE)
    {
        if (C->last_use_ms && now - C->last_use_ms > IDLE_CLOSE_MS)
        {
            close_link();
            close_serial();
            C->last_use_ms = 0;
        }
        return;
    }
    uint32_t limit = C->rtu ? RTU_TIMEOUT_MS : TCP_TIMEOUT_MS;
    if (now - C->start_ms > limit)
    {
        if (!C->rtu)
            close_link();
        finish(-2, C->state == C_CONNECTING ? "No connection (timeout)" : "No answer (timeout)");
        return;
    }
    char err[64];
    switch (C->state)
    {
    case C_CONNECTING:
    {
        int r = td_sock_connect_poll(C->sock, err, sizeof(err));
        if (r < 0)
        {
            close_link();
            finish(-1, err);
        }
        else if (r > 0)
        {
            C->state = C_SENDING;
        }
        return;
    }
    case C_SENDING:
    {
        int n = C->rtu ? s_serial->write(C->frame + C->sent, C->frame_len - C->sent)
                       : td_sock_send(C->sock, C->frame + C->sent, C->frame_len - C->sent);
        if (n < 0)
        {
            if (!C->rtu)
                close_link();
            finish(-1, C->rtu ? "Serial write failed" : "Connection lost");
            return;
        }
        C->sent += n;
        if (C->sent >= C->frame_len)
            C->state = C_WAITING;
        return;
    }
    case C_WAITING:
    {
        int n = C->rtu ? s_serial->read(C->in + C->in_len, FRAME_MAX - C->in_len)
                       : td_sock_recv(C->sock, C->in + C->in_len, FRAME_MAX - C->in_len);
        if (n < 0)
        {
            if (!C->rtu)
                close_link();
            finish(-1, C->rtu ? "Serial read failed" : "Connection closed by the device");
            return;
        }
        C->in_len += n;
        if (C->rtu)
        {
            int want = rtu_expected(C->in, C->in_len);
            if (!want || C->in_len < want)
                return;
            if (C->in[0] != C->req.unit)
            {
                finish(-3, "Answer from another unit");
                return;
            }
            uint16_t crc = td_mb_crc16(C->in, want - 2);
            if (C->in[want - 2] != (uint8_t)crc || C->in[want - 1] != (uint8_t)(crc >> 8))
            {
                finish(-3, "CRC error in the answer");
                return;
            }
            td_mb_decode_pdu(&C->req, C->in + 1, want - 3, &C->res);
            finish(C->res.status, NULL);
        }
        else
        {
            while (C->in_len >= 7)
            {
                int want = 6 + be16(C->in + 4);
                if (want < 8 || want > FRAME_MAX)
                {
                    close_link();
                    finish(-3, "Bad frame from the device");
                    return;
                }
                if (C->in_len < want)
                    return;
                if (be16(C->in) != C->tid)
                {          /* a late answer: drop it */
                    memmove(C->in, C->in + want, (size_t)(C->in_len - want));
                    C->in_len -= want;
                    continue;
                }
                td_mb_decode_pdu(&C->req, C->in + 7, want - 7, &C->res);
                finish(C->res.status, NULL);
                return;
            }
        }
        return;
    }
    default:
        return;
    }
}

bool td_mb_result(int ticket, td_mb_result_t *out)
{
    bool done = false;
    td_proto_lock();
    if (C && C->ticket == ticket && C->state == C_DONE)
    {
        *out = C->res;
        done = true;
    }
    td_proto_unlock();
    return done;
}

bool td_mb_busy(void)
{
    td_proto_lock();
    bool b = C && C->state != C_IDLE && C->state != C_DONE;
    td_proto_unlock();
    return b;
}

bool td_mb_transact(const td_mb_request_t *req, td_mb_result_t *out, char *err, size_t cap)
{
    int t = 0;
    for (int tries = 0; tries < 100 && !(t = td_mb_submit(req, err, cap)); tries++)
    {
        if (!strstr(err, "Busy"))
            return false;
        td_mb_poll();
        td_proto_sleep_ms(20);
    }
    if (!t)
        return false;
    uint32_t start = td_proto_millis();
    while (td_proto_millis() - start < TCP_TIMEOUT_MS + 3000)
    {
        td_mb_poll();
        if (td_mb_result(t, out))
            return true;
        td_proto_sleep_ms(5);
    }
    snprintf(err, cap, "The request was replaced by another one");
    return false;
}

/* ------------------------------------------------------------- server */

typedef struct
{
    td_sock_t sock;
    uint8_t in[FRAME_MAX];
    int in_len;
} sclient_t;

typedef struct
{
    td_sock_t listener;
    uint16_t port;
    uint32_t requests;
    sclient_t clients[SERVER_CLIENTS];
    td_mb_tables_t tables;
} server_t;

static server_t *S;

bool td_mb_server_start(uint16_t port, char *err, size_t cap)
{
    if (!port)
        port = TD_MB_TCP_PORT;
    td_proto_lock();
    if (S && S->port == port)
    {
        td_proto_unlock();
        return true;
    }
    td_sock_t l = td_sock_listen(port, err, cap);
    if (l == TD_SOCK_INVALID)
    {
        td_proto_unlock();
        return false;
    }
    if (!S)
    {
        S = calloc(1, sizeof(*S));
        if (!S)
        {
            td_sock_close(l);
            td_proto_unlock();
            snprintf(err, cap, "Not enough memory");
            return false;
        }
        for (int i = 0; i < SERVER_CLIENTS; i++)
            S->clients[i].sock = TD_SOCK_INVALID;
    }
    else if (S->listener != TD_SOCK_INVALID)
    {
        td_sock_close(S->listener);
    }
    S->listener = l;
    S->port = port;
    td_proto_unlock();
    return true;
}

void td_mb_server_stop(void)
{
    td_proto_lock();
    if (S)
    {
        td_sock_close(S->listener);
        for (int i = 0; i < SERVER_CLIENTS; i++)
            td_sock_close(S->clients[i].sock);
        free(S);
        S = NULL;
    }
    td_proto_unlock();
}

bool td_mb_server_status(uint16_t *port, int *clients, uint32_t *requests)
{
    td_proto_lock();
    bool on = S != NULL;
    int n = 0;
    if (S)
        for (int i = 0; i < SERVER_CLIENTS; i++)
            n += S->clients[i].sock != TD_SOCK_INVALID;
    if (port)
        *port = S ? S->port : 0;
    if (clients)
        *clients = n;
    if (requests)
        *requests = S ? S->requests : 0;
    td_proto_unlock();
    return on;
}

int td_mb_server_get(td_mb_table_t t, uint16_t addr, uint16_t count, uint16_t *out)
{
    int n = 0;
    td_proto_lock();
    if (S)
    {
        for (; n < count && addr + n < TD_MB_TABLE_SIZE; n++)
        {
            int i = addr + n;
            switch (t)
            {
            case TD_MB_COILS:
                out[n] = get_bit(S->tables.coils, i);
                break;
            case TD_MB_DISCRETE:
                out[n] = get_bit(S->tables.discrete, i);
                break;
            case TD_MB_HOLDING:
                out[n] = S->tables.holding[i];
                break;
            case TD_MB_INPUT:
                out[n] = S->tables.input[i];
                break;
            }
        }
    }
    td_proto_unlock();
    return n;
}

int td_mb_server_set(td_mb_table_t t, uint16_t addr, uint16_t count, const uint16_t *in)
{
    int n = 0;
    td_proto_lock();
    if (S)
    {
        for (; n < count && addr + n < TD_MB_TABLE_SIZE; n++)
        {
            int i = addr + n;
            switch (t)
            {
            case TD_MB_COILS:
                set_bit(S->tables.coils, i, in[n] != 0);
                break;
            case TD_MB_DISCRETE:
                set_bit(S->tables.discrete, i, in[n] != 0);
                break;
            case TD_MB_HOLDING:
                S->tables.holding[i] = in[n];
                break;
            case TD_MB_INPUT:
                S->tables.input[i] = in[n];
                break;
            }
        }
    }
    td_proto_unlock();
    return n;
}

static void drop_client(sclient_t *c)
{
    td_sock_close(c->sock);
    c->sock = TD_SOCK_INVALID;
    c->in_len = 0;
}

static void server_poll(void)
{
    if (!S)
        return;
    td_sock_t n = td_sock_accept(S->listener, NULL, 0);
    if (n != TD_SOCK_INVALID)
    {
        sclient_t *slot = NULL;
        for (int i = 0; i < SERVER_CLIENTS && !slot; i++)
            if (S->clients[i].sock == TD_SOCK_INVALID)
                slot = &S->clients[i];
        if (slot)
        {
            slot->sock = n;
            slot->in_len = 0;
        }
        else
        {
            td_sock_close(n);                        /* full */
        }
    }
    for (int i = 0; i < SERVER_CLIENTS; i++)
    {
        sclient_t *c = &S->clients[i];
        if (c->sock == TD_SOCK_INVALID)
            continue;
        int got = td_sock_recv(c->sock, c->in + c->in_len, FRAME_MAX - c->in_len);
        if (got < 0)
        {
            drop_client(c);
            continue;
        }
        c->in_len += got;
        while (c->in_len >= 7)
        {
            int want = 6 + be16(c->in + 4);
            if (want < 8 || want > FRAME_MAX || be16(c->in + 2) != 0)
            {
                drop_client(c);
                break;
            }
            if (c->in_len < want)
                break;
            uint8_t out[FRAME_MAX];
            memcpy(out, c->in, 7);                   /* same tid, pid, unit */
            int rl = td_mb_serve_pdu(&S->tables, c->in + 7, want - 7, out + 7);
            put16(out + 4, (uint16_t)(rl + 1));
            S->requests++;
            if (td_sock_send(c->sock, out, rl + 7) < 0)
            {
                drop_client(c);
                break;
            }
            memmove(c->in, c->in + want, (size_t)(c->in_len - want));
            c->in_len -= want;
        }
    }
}

void td_mb_poll(void)
{
    if (!C && !S)
        return;                             /* cheap when unused */
    td_proto_lock();
    client_poll();
    server_poll();
    td_proto_unlock();
}
