/*
 * td_mqtt.h - a small MQTT 3.1.1 client (TCP or TLS, QoS 0 and 1).
 *
 * There is one connection per device, shared by the MQTT app and the
 * `mqtt` shell command. Nothing blocks except td_mqtt_connect(), which may
 * wait for DNS: td_mqtt_poll() drives the connection and must be called
 * often (the desktop calls it from its main loop). All functions are safe
 * to call from any task.
 *
 * Memory (about 7 KB) is allocated on connect and freed on disconnect.
 */
#ifndef TD_MQTT_H
#define TD_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TD_MQTT_DEFAULT_PORT 1883
#define TD_MQTT_TLS_PORT     8883
#define TD_MQTT_PATH_MAX     160      /* certificate file paths (real paths) */
#define TD_MQTT_CERT_MAX     16384    /* largest certificate / key file read */
#define TD_MQTT_TOPIC_MAX    64      /* longer topics are refused (sub/pub) */
#define TD_MQTT_PAYLOAD_KEEP 120  /* bytes of each message kept in the log */
#define TD_MQTT_LOG          20            /* messages kept */
#define TD_MQTT_SUBS         8            /* subscriptions */

    typedef struct
    {
        char host[64];
        uint16_t port;            /* 0: 1883, or 8883 with TLS */
        char client_id[32];       /* "" = tinydesk-<random> */
        char user[32];            /* "" = no login */
        char pass[64];
        uint16_t keepalive;       /* seconds; 0 = 60 */
        bool auto_reconnect;      /* retry every 5 s after a drop */
        bool persistent;          /* clean_session off: the broker keeps our session */

    /* TLS (mqtts). Files are real paths; empty ca_file = the system's
     * trusted roots. */
        bool tls;
        bool insecure;            /* do not verify the server certificate */
        char ca_file[TD_MQTT_PATH_MAX];
        char cert_file[TD_MQTT_PATH_MAX];
        char key_file[TD_MQTT_PATH_MAX];
        char key_pass[64];

    /* Last will, sent by the broker if we vanish. */
        char will_topic[TD_MQTT_TOPIC_MAX];
        char will_payload[128];
        uint8_t will_qos;
        bool will_retain;

    /* Subscriptions made on connect. */
        int nsubs;
        char sub_topic[TD_MQTT_SUBS][TD_MQTT_TOPIC_MAX];
        uint8_t sub_qos[TD_MQTT_SUBS];
    } td_mqtt_config_t;

/* Parse "host", "host:port", "mqtt://host[:port]" or "mqtts://host[:port]"
 * into cfg (host, port, tls). */
    bool td_mqtt_parse_broker(const char *text, td_mqtt_config_t *cfg);

/* Map a path written in a config file ("~/certs/ca.crt", "/root/x",
 * "ca.crt" relative to the file) to a real path; false if not allowed. */
    typedef bool (*td_mqtt_resolve_fn)(const char *path, char *real, size_t cap, void *ctx);

/* Read a config file (see README "MQTT config file"). `path` is given to
 * resolve() first; relative paths inside are relative to its directory.
 * Returns false with "line N: ..." in err. */
    bool td_mqtt_load_config(const char *path, td_mqtt_resolve_fn resolve, void *ctx, td_mqtt_config_t *cfg,
                             char *err, size_t cap);

/* A commented example config, for `mqtt config init` and the app. */
    extern const char td_mqtt_config_template[];

    typedef enum
    {
        TD_MQTT_OFF,              /* not connected (see td_mqtt_status for why) */
        TD_MQTT_CONNECTING,       /* TCP connect */
        TD_MQTT_TLS_HANDSHAKE,
        TD_MQTT_WAIT_CONNACK,
        TD_MQTT_CONNECTED,
        TD_MQTT_RETRY,            /* waiting to reconnect */
    } td_mqtt_state_t;

    typedef struct
    {
        uint32_t seq;             /* 1, 2, 3... per connection */
        uint32_t ms;              /* td_proto_millis() when it arrived / was sent */
        int64_t utc;              /* wall clock then (seconds since 1970), 0 if unset */
        bool outgoing;            /* our own publish */
        uint8_t qos;
        bool retain;
        uint16_t len;             /* full payload length */
        char topic[TD_MQTT_TOPIC_MAX];
        char payload[TD_MQTT_PAYLOAD_KEEP + 1];   /* NUL-terminated, cut */
    } td_mqtt_msg_t;

    typedef struct
    {
        td_mqtt_state_t state;
        char text[80];            /* "Connected to 1.2.3.4:1883", or the last error */
        char broker[72];          /* host:port given */
        uint32_t rx, tx;          /* messages received / published */
        uint32_t connected_ms;    /* since when (td_proto_millis) */
        int subs;
        bool tls;
        char security[72];        /* "TLSv1.2 TLS-ECDHE-..." once connected */
    } td_mqtt_status_t;

/* Start connecting (resolves the host first; may block for DNS). Returns
 * false with a message in err. A running connection is closed first. */
    bool td_mqtt_connect(const td_mqtt_config_t *cfg, char *err, size_t cap);
    void td_mqtt_disconnect(void);
    void td_mqtt_status(td_mqtt_status_t *out);

/* Subscriptions are remembered and sent again after a reconnect.
 * 0 on success, -1 bad topic or not connected, -2 table full. */
    int td_mqtt_subscribe(const char *topic, int qos);
    int td_mqtt_unsubscribe(const char *topic);
/* Subscription i (0..subs-1): copies its topic and QoS; false past the end. */
    bool td_mqtt_subscription(int i, char *topic, size_t cap, int *qos);

/* 0 on success, -1 not connected / bad topic, -2 too big or busy. */
    int td_mqtt_publish(const char *topic, const void *payload, int len, int qos, bool retain);

/* The newest message's sequence number (0: none), and a copy of message
 * `seq` if it is still in the log. */
    uint32_t td_mqtt_last_seq(void);
    bool td_mqtt_message(uint32_t seq, td_mqtt_msg_t *out);

/* Drive the connection: connect, read, keep alive. */
    void td_mqtt_poll(void);

/* --- exposed for tests ------------------------------------------------ */
    int td_mqtt_encode_length(uint32_t len, uint8_t out[4]);
/* Decode a remaining length. >0: bytes used, 0: need more, -1: invalid. */
    int td_mqtt_decode_length(const uint8_t *p, int avail, uint32_t *len);
    bool td_mqtt_topic_matches(const char *filter, const char *topic);

#ifdef __cplusplus
}
#endif

#endif
