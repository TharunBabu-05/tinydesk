/*
 * td_mqtt_conf.c - the MQTT client config file and broker URLs.
 *
 * One setting per line, "key value", '#' starts a comment. Key names follow
 * the Mosquitto tools where they exist (cafile, certfile, keyfile,
 * tls_insecure, will_topic, ...). See td_mqtt_config_template below.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_mqtt.h"
#include "td_sock.h"
#include "td_tls.h"

const char td_mqtt_config_template[] =
    "# MQTT client settings (TinyDesk). One setting per line; '#' starts a comment.\n"
    "# Used by 'mqtt connect' with no broker, 'mqtt connect -c <file>' and the\n"
    "# MQTT app (type the file name, e.g. ~/mqtt.conf, as the broker).\n"
    "\n"
    "# Where: host and port, or a URL (mqtt://host:1883, mqtts://host:8883).\n"
    "host localhost\n"
    "#port 1883\n"
    "#url mqtts://broker.example.com:8883\n"
    "\n"
    "# Who\n"
    "#client_id tinydesk-kitchen\n"
    "#username bob\n"
    "#password secret\n"
    "\n"
    "# TLS. Without cafile the device's trusted roots are used (ESP32: the\n"
    "# ESP-IDF certificate bundle; Windows: the ROOT store; Linux: the system\n"
    "# bundle). Paths: ~/..., /absolute, or relative to this file. PEM or DER.\n"
    "#tls on\n"
    "#cafile ~/certs/ca.crt\n"
    "#certfile ~/certs/client.crt\n"
    "#keyfile ~/certs/client.key\n"
    "#key_password secret\n"
    "#tls_insecure off          # on: do not check the server certificate\n"
    "\n"
    "# Session\n"
    "#keepalive 60\n"
    "#reconnect on              # try again every 5 s after a drop\n"
    "#clean_session on          # off: the broker keeps our subscriptions/messages\n"
    "\n"
    "# Last will: published by the broker if this device disappears.\n"
    "#will_topic devices/tinydesk/status\n"
    "#will_payload offline\n"
    "#will_qos 1\n"
    "#will_retain on\n"
    "\n"
    "# Topics to subscribe to after connecting: subscribe <topic> [qos]\n"
    "#subscribe devices/+/status 1\n"
    "#subscribe tinydesk/#\n";

bool td_mqtt_parse_broker(const char *text, td_mqtt_config_t *cfg)
{
    const char *p = text;
    bool tls = cfg->tls;
    if (strncmp(p, "mqtts://", 8) == 0 || strncmp(p, "ssl://", 6) == 0 || strncmp(p, "tls://", 6) == 0)
    {
        tls = true;
        p = strstr(p, "://") + 3;
    }
    else if (strncmp(p, "mqtt://", 7) == 0 || strncmp(p, "tcp://", 6) == 0)
    {
        tls = false;
        p = strstr(p, "://") + 3;
    }
    else if (strstr(p, "://"))
    {
        return false;                             /* ws:// and friends */
    }
    char hostport[80];
    snprintf(hostport, sizeof(hostport), "%s", p);
    char *slash = strchr(hostport, '/');
    if (slash)
        *slash = '\0';                     /* mqtt://host/ */
    uint16_t port;
    if (!td_split_host_port(hostport, cfg->host, sizeof(cfg->host), &port, 0))
        return false;
    cfg->tls = tls;
    cfg->port = port;                             /* 0: the default for tls */
    return true;
}

/* ------------------------------------------------------------ parsing */

static bool parse_bool(const char *v, bool *out)
{
    if (!strcmp(v, "on") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "1"))
        *out = true;
    else if (!strcmp(v, "off") || !strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "0"))
        *out = false;
    else
        return false;
    return true;
}

static bool copy(char *dst, size_t cap, const char *v)
{
    if (strlen(v) >= cap)
        return false;
    memcpy(dst, v, strlen(v) + 1);
    return true;
}

/* A path from the file: relative ones are taken from the file's folder. */
static bool file_path(const char *v, const char *conf_path, td_mqtt_resolve_fn resolve, void *ctx, char *real,
                      size_t cap)
{
    char joined[TD_MQTT_PATH_MAX * 2];
    if (v[0] == '/' || v[0] == '~' || !strrchr(conf_path, '/'))
    {
        snprintf(joined, sizeof(joined), "%s", v);
    }
    else
    {
        int dir = (int)(strrchr(conf_path, '/') - conf_path);
        snprintf(joined, sizeof(joined), "%.*s/%s", dir, conf_path, v);
    }
    return resolve(joined, real, cap, ctx);
}

bool td_mqtt_load_config(const char *path, td_mqtt_resolve_fn resolve, void *ctx, td_mqtt_config_t *cfg,
                         char *err, size_t cap)
{
    char real[TD_MQTT_PATH_MAX];
    if (!resolve(path, real, sizeof(real), ctx))
    {
        snprintf(err, cap, "%.60s: not allowed here", path);
        return false;
    }
    size_t len = 0;
    char *text = td_read_file(real, 8192, &len, err, cap);
    if (!text)
        return false;

    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->host, sizeof(cfg->host), "localhost");
    bool ok = true;
    int line_no = 0;
    char *next = text;
    while (next && ok)
    {
        char *line = next;
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        next = nl ? nl + 1 : NULL;
        line_no++;
        /* Key and value; '#' at the start makes the line a comment. */
        char *key = line;
        while (isspace((unsigned char)*key))
            key++;
        if (!*key || *key == '#')
            continue;
        char *val = key;
        while (*val && !isspace((unsigned char)*val))
            val++;
        if (*val)
            *val++ = '\0';
        while (isspace((unsigned char)*val))
            val++;
        for (char *k = key; *k; k++)
            *k = (char)tolower((unsigned char)*k);
        /* An inline comment is " # " (space, hash, space), except on topic
         * lines, where # is the MQTT wildcard. */
        bool topic_line = !strcmp(key, "subscribe") || !strcmp(key, "sub") || !strcmp(key, "will_topic");
        if (!topic_line)
        {
            for (char *h = val; *h; h++)
                if (*h == '#' && (h == val || isspace((unsigned char)h[-1])) &&
                    (!h[1] || isspace((unsigned char)h[1])))
                {
                    *h = '\0';
                    break;
                }
        }
        size_t n = strlen(val);                    /* trailing blanks and the CR */
        while (n && isspace((unsigned char)val[n - 1]))
            val[--n] = '\0';
        if (val[0] == '"' && n >= 2 && val[n - 1] == '"')
        {   /* "quoted value" */
            val[n - 1] = '\0';
            val++;
        }

        const char *bad = NULL;
        bool b;
        if (!strcmp(key, "host") || !strcmp(key, "broker"))
        {
            bool good = val[0] && (strchr(val, ':') ? td_mqtt_parse_broker(val, cfg)
                                                    : copy(cfg->host, sizeof(cfg->host), val));
            if (!good)
                bad = "bad host";
        }
        else if (!strcmp(key, "url"))
        {
            if (!td_mqtt_parse_broker(val, cfg))
                bad = "url must be mqtt://host[:port] or mqtts://host[:port]";
        }
        else if (!strcmp(key, "port"))
        {
            long p = strtol(val, NULL, 10);
            if (p < 1 || p > 65535)
                bad = "port must be 1..65535";
            else
                cfg->port = (uint16_t)p;
        }
        else if (!strcmp(key, "username") || !strcmp(key, "user"))
        {
            if (!copy(cfg->user, sizeof(cfg->user), val))
                bad = "username too long";
        }
        else if (!strcmp(key, "password") || !strcmp(key, "pass"))
        {
            if (!copy(cfg->pass, sizeof(cfg->pass), val))
                bad = "password too long";
        }
        else if (!strcmp(key, "client_id") || !strcmp(key, "id"))
        {
            if (!copy(cfg->client_id, sizeof(cfg->client_id), val))
                bad = "client_id too long (31 at most)";
        }
        else if (!strcmp(key, "keepalive"))
        {
            long k = strtol(val, NULL, 10);
            if (k < 5 || k > 65535)
                bad = "keepalive must be 5..65535 seconds";
            else
                cfg->keepalive = (uint16_t)k;
        }
        else if (!strcmp(key, "reconnect") || !strcmp(key, "auto_reconnect"))
        {
            if (!parse_bool(val, &cfg->auto_reconnect))
                bad = "use on or off";
        }
        else if (!strcmp(key, "clean_session"))
        {
            if (!parse_bool(val, &b))
                bad = "use on or off";
            else
                cfg->persistent = !b;
        }
        else if (!strcmp(key, "tls") || !strcmp(key, "ssl"))
        {
            if (!parse_bool(val, &cfg->tls))
                bad = "use on or off";
        }
        else if (!strcmp(key, "tls_insecure") || !strcmp(key, "insecure"))
        {
            if (!parse_bool(val, &cfg->insecure))
                bad = "use on or off";
        }
        else if (!strcmp(key, "cafile") || !strcmp(key, "ca_file") || !strcmp(key, "ca"))
        {
            if (!file_path(val, path, resolve, ctx, cfg->ca_file, sizeof(cfg->ca_file)))
                bad = "cafile not allowed";
            cfg->tls = true;
        }
        else if (!strcmp(key, "certfile") || !strcmp(key, "cert"))
        {
            if (!file_path(val, path, resolve, ctx, cfg->cert_file, sizeof(cfg->cert_file)))
                bad = "certfile not allowed";
            cfg->tls = true;
        }
        else if (!strcmp(key, "keyfile") || !strcmp(key, "key"))
        {
            if (!file_path(val, path, resolve, ctx, cfg->key_file, sizeof(cfg->key_file)))
                bad = "keyfile not allowed";
            cfg->tls = true;
        }
        else if (!strcmp(key, "key_password") || !strcmp(key, "keypass"))
        {
            if (!copy(cfg->key_pass, sizeof(cfg->key_pass), val))
                bad = "key_password too long";
        }
        else if (!strcmp(key, "will_topic"))
        {
            if (!copy(cfg->will_topic, sizeof(cfg->will_topic), val) || strchr(val, '+') || strchr(val, '#'))
                bad = "will_topic: up to 63 characters, no + or #";
        }
        else if (!strcmp(key, "will_payload") || !strcmp(key, "will_message"))
        {
            if (!copy(cfg->will_payload, sizeof(cfg->will_payload), val))
                bad = "will_payload too long (127 at most)";
        }
        else if (!strcmp(key, "will_qos"))
        {
            if (strcmp(val, "0") && strcmp(val, "1"))
                bad = "will_qos must be 0 or 1";
            else
                cfg->will_qos = (uint8_t)(val[0] - '0');
        }
        else if (!strcmp(key, "will_retain"))
        {
            if (!parse_bool(val, &cfg->will_retain))
                bad = "use on or off";
        }
        else if (!strcmp(key, "subscribe") || !strcmp(key, "sub"))
        {
            char *q = val;
            while (*q && !isspace((unsigned char)*q))
                q++;
            int qos = 0;
            if (*q)
            {
                *q++ = '\0';
                while (isspace((unsigned char)*q))
                    q++;
                qos = atoi(q);
            }
            if (cfg->nsubs >= TD_MQTT_SUBS)
                bad = "8 subscriptions at most";
            else if (!copy(cfg->sub_topic[cfg->nsubs], TD_MQTT_TOPIC_MAX, val) || !val[0])
                bad = "bad topic";
            else
                cfg->sub_qos[cfg->nsubs++] = (uint8_t)(qos > 0 ? 1 : 0);
        }
        else
        {
            bad = "unknown setting";
        }
        if (bad)
        {
            snprintf(err, cap, "line %d (%.20s): %s", line_no, key, bad);
            ok = false;
        }
    }
    memset(text, 0, len);             /* it may hold passwords */
    free(text);
    if (ok && !cfg->host[0])
    {
        snprintf(err, cap, "no host set");
        ok = false;
    }
    if (ok && (cfg->cert_file[0] != 0) != (cfg->key_file[0] != 0))
    {
        snprintf(err, cap, "certfile and keyfile go together");
        ok = false;
    }
    if (!ok)
        memset(cfg->pass, 0, sizeof(cfg->pass));
    return ok;
}
