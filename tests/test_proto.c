/*
 * test_proto.c - MQTT encoding and topic matching, Modbus PDUs, CRC, and
 * Modbus round trips over a fake RTU line and over TCP on loopback.
 */
#include <stdint.h>

#include "td_test.h"
#include "td_modbus.h"
#include "td_mqtt.h"
#include "td_sock.h"

static void test_crc(void)
{
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    CHECK_EQ(td_mb_crc16(frame, 6), 0xCDC5);        /* sent as C5 CD */
    const uint8_t f2[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};
    CHECK_EQ(td_mb_crc16(f2, 6), 0x8776);            /* the spec's example: 76 87 */
}

static void test_mqtt_length(void)
{
    uint8_t b[4];
    uint32_t v;
    CHECK_EQ(td_mqtt_encode_length(0, b), 1);
    CHECK_EQ(b[0], 0);
    CHECK_EQ(td_mqtt_encode_length(127, b), 1);
    CHECK_EQ(b[0], 0x7F);
    CHECK_EQ(td_mqtt_encode_length(128, b), 2);
    CHECK(b[0] == 0x80 && b[1] == 0x01);
    CHECK_EQ(td_mqtt_encode_length(16383, b), 2);
    CHECK(b[0] == 0xFF && b[1] == 0x7F);
    CHECK_EQ(td_mqtt_encode_length(2097152, b), 4);
    CHECK_EQ(td_mqtt_decode_length(b, 4, &v), 4);
    CHECK_EQ(v, 2097152);
    CHECK_EQ(td_mqtt_decode_length(b, 2, &v), 0);    /* need more */
    const uint8_t bad[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    CHECK_EQ(td_mqtt_decode_length(bad, 5, &v), -1);
}

static void test_topics(void)
{
    CHECK(td_mqtt_topic_matches("a/b", "a/b"));
    CHECK(!td_mqtt_topic_matches("a/b", "a/c"));
    CHECK(td_mqtt_topic_matches("a/+/c", "a/b/c"));
    CHECK(!td_mqtt_topic_matches("a/+", "a/b/c"));
    CHECK(td_mqtt_topic_matches("a/#", "a/b/c"));
    CHECK(td_mqtt_topic_matches("a/#", "a"));
    CHECK(td_mqtt_topic_matches("#", "x/y"));
    CHECK(!td_mqtt_topic_matches("#", "$SYS/x"));
    CHECK(!td_mqtt_topic_matches("a", "ab"));
}

static void test_pdu(void)
{
    static td_mb_tables_t t;
    uint8_t resp[260];
    td_mb_request_t r = {.fc = 16, .addr = 10, .nvalues = 3, .values = {1, 2, 0xBEEF}};
    uint8_t pdu[260];
    int n = td_mb_build_pdu(&r, pdu);
    CHECK_EQ(n, 6 + 6);
    CHECK_EQ(td_mb_serve_pdu(&t, pdu, n, resp), 5);
    CHECK_EQ(t.holding[12], 0xBEEF);

    td_mb_request_t rd = {.fc = 3, .addr = 10, .count = 3};
    n = td_mb_build_pdu(&rd, pdu);
    int rl = td_mb_serve_pdu(&t, pdu, n, resp);
    td_mb_result_t res;
    td_mb_decode_pdu(&rd, resp, rl, &res);
    CHECK_EQ(res.status, 0);
    CHECK_EQ(res.count, 3);
    CHECK_EQ(res.values[2], 0xBEEF);

    /* Coils: write 3 with fc15, one with fc5, read back 8. */
    td_mb_request_t wc = {.fc = 15, .addr = 0, .nvalues = 3, .values = {1, 0, 1}};
    n = td_mb_build_pdu(&wc, pdu);
    CHECK_EQ(td_mb_serve_pdu(&t, pdu, n, resp), 5);
    td_mb_request_t w1 = {.fc = 5, .addr = 7, .nvalues = 1, .values = {1}};
    n = td_mb_build_pdu(&w1, pdu);
    CHECK_EQ(td_mb_serve_pdu(&t, pdu, n, resp), 5);
    td_mb_request_t rc = {.fc = 1, .addr = 0, .count = 8};
    n = td_mb_build_pdu(&rc, pdu);
    rl = td_mb_serve_pdu(&t, pdu, n, resp);
    td_mb_decode_pdu(&rc, resp, rl, &res);
    CHECK_EQ(res.status, 0);
    CHECK(res.values[0] == 1 && res.values[1] == 0 && res.values[2] == 1 && res.values[7] == 1);

    /* Exceptions. */
    td_mb_request_t far = {.fc = 3, .addr = 127, .count = 2};
    n = td_mb_build_pdu(&far, pdu);
    rl = td_mb_serve_pdu(&t, pdu, n, resp);
    td_mb_decode_pdu(&far, resp, rl, &res);
    CHECK_EQ(res.status, 2);                         /* illegal data address */
    const uint8_t weird[] = {0x2B, 0x0E, 0x01, 0x00, 0x00};
    CHECK_EQ(td_mb_serve_pdu(&t, weird, 5, resp), 2);
    CHECK_EQ(resp[0], 0xAB);
    CHECK_EQ(resp[1], 1);                            /* illegal function */

    td_mb_table_t tb;
    CHECK(td_mb_parse_table("hr", &tb) && tb == TD_MB_HOLDING);
    CHECK(td_mb_parse_table("coils", &tb) && tb == TD_MB_COILS);
    CHECK(!td_mb_parse_table("xx", &tb));
}

/* --------------------------------------------- fake RTU line + slave */

static td_mb_tables_t s_slave;
static uint8_t s_req[260], s_ans[260];
static int s_req_len, s_ans_len, s_ans_pos;
static bool s_corrupt;

static int s_line;

static bool fake_open(int port, uint32_t baud, char parity, int stop, char *err, size_t cap)
{
    (void)baud;
    (void)parity;
    (void)stop;
    (void)err;
    (void)cap;
    s_line = port;
    return true;
}

static int fake_write(const uint8_t *buf, int len)
{
    memcpy(s_req + s_req_len, buf, (size_t)len);
    s_req_len += len;
    /* A whole request: unit + PDU + CRC. Answer as unit 7 would. */
    uint16_t crc = td_mb_crc16(s_req, s_req_len - 2);
    if (s_req_len >= 8 && s_req[s_req_len - 2] == (uint8_t)crc && s_req[s_req_len - 1] == (uint8_t)(crc >> 8))
    {
        s_ans[0] = s_req[0];
        int rl = td_mb_serve_pdu(&s_slave, s_req + 1, s_req_len - 3, s_ans + 1);
        uint16_t c = td_mb_crc16(s_ans, rl + 1);
        s_ans[rl + 1] = (uint8_t)c;
        s_ans[rl + 2] = (uint8_t)(c >> 8);
        if (s_corrupt)
            s_ans[rl + 2] ^= 0x55;
        s_ans_len = rl + 3;
        s_ans_pos = 0;
        s_req_len = 0;
    }
    return len;
}

static int fake_read(uint8_t *buf, int cap)
{
    int n = s_ans_len - s_ans_pos;
    if (n > 3)
        n = 3;                 /* dribble in, like a real line */
    if (n > cap)
        n = cap;
    memcpy(buf, s_ans + s_ans_pos, (size_t)n);
    s_ans_pos += n;
    return n;
}

static void fake_close(void)
{
}

static const char *fake_name(int port)
{
    (void)port;
    return "fake";
}

static const td_mb_serial_t s_fake = {2, fake_name, fake_open, fake_write, fake_read, fake_close};

static void test_rtu(void)
{
    td_mb_set_serial(&s_fake);
    s_slave.input[5] = 1234;
    td_mb_request_t r = {.unit = 7, .fc = 4, .addr = 5, .count = 2};
    snprintf(r.target, sizeof(r.target), "rtu:19200:8E1");
    td_mb_result_t res;
    char err[80] = "";
    CHECK(td_mb_transact(&r, &res, err, sizeof(err)));
    CHECK_EQ(res.status, 0);
    CHECK_EQ(res.values[0], 1234);
    CHECK_EQ(s_line, 1);                             /* "rtu" is line 1 */

    snprintf(r.target, sizeof(r.target), "rtu2");
    CHECK(td_mb_transact(&r, &res, err, sizeof(err)));
    CHECK_EQ(s_line, 2);
    snprintf(r.target, sizeof(r.target), "rtu3");
    CHECK(!td_mb_transact(&r, &res, err, sizeof(err)));   /* only 2 lines */
    snprintf(r.target, sizeof(r.target), "rtu1:19200:8E1");

    s_corrupt = true;
    CHECK(td_mb_transact(&r, &res, err, sizeof(err)));
    CHECK_EQ(res.status, -3);                        /* CRC error */
    s_corrupt = false;

    snprintf(r.target, sizeof(r.target), "rtu:12");
    CHECK(!td_mb_transact(&r, &res, err, sizeof(err)));   /* bad baud rate */
}

static void test_tcp(void)
{
    char err[80] = "";
    uint16_t port = 15020;
    bool up = td_mb_server_start(port, err, sizeof(err));
    CHECK(up);
    if (!up)
    {
        printf("server: %s\n", err);
        return;
    }
    td_mb_request_t w = {.unit = 1, .fc = 16, .addr = 0, .nvalues = 2, .values = {42, 4242}};
    snprintf(w.target, sizeof(w.target), "127.0.0.1:%u", (unsigned)port);
    td_mb_result_t res;
    CHECK(td_mb_transact(&w, &res, err, sizeof(err)));
    CHECK_EQ(res.status, 0);

    uint16_t v[2] = {0, 0};
    CHECK_EQ(td_mb_server_get(TD_MB_HOLDING, 0, 2, v), 2);
    CHECK(v[0] == 42 && v[1] == 4242);

    td_mb_request_t r = w;
    r.fc = 3;
    r.count = 2;
    CHECK(td_mb_transact(&r, &res, err, sizeof(err)));   /* reuses the link */
    CHECK_EQ(res.status, 0);
    CHECK_EQ(res.values[1], 4242);

    r.addr = 200;
    CHECK(td_mb_transact(&r, &res, err, sizeof(err)));
    CHECK_EQ(res.status, 2);

    int clients = 0;
    uint32_t reqs = 0;
    CHECK(td_mb_server_status(NULL, &clients, &reqs));
    CHECK_EQ(clients, 1);
    CHECK_EQ(reqs, 3);
    td_mb_server_stop();
    CHECK(!td_mb_server_status(NULL, NULL, NULL));
}

static void test_split(void)
{
    char h[64];
    uint16_t p;
    CHECK(td_split_host_port("10.0.0.5", h, sizeof(h), &p, 502) && p == 502 && !strcmp(h, "10.0.0.5"));
    CHECK(td_split_host_port("broker:1884", h, sizeof(h), &p, 1883) && p == 1884 && !strcmp(h, "broker"));
    CHECK(td_split_host_port("[::1]:80", h, sizeof(h), &p, 1) && p == 80 && !strcmp(h, "::1"));
    CHECK(!td_split_host_port("host:0", h, sizeof(h), &p, 1));
    CHECK(!td_split_host_port("", h, sizeof(h), &p, 1));
}

/* ------------------------------------------------ MQTT config files */

/* Test resolver: paths are real already; ".." is refused. */
static bool plain_resolve(const char *path, char *real, size_t cap, void *ctx)
{
    (void)ctx;
    if (strstr(path, ".."))
        return false;
    snprintf(real, cap, "%s", path);
    return true;
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fputs(text, f);
    return fclose(f) == 0;
}

static void test_config(void)
{
    const char *path = "test_mqtt_tmp.conf";
    CHECK(write_text(path,
                     "# comment line\n"
                     "url mqtts://broker.example.com\r\n"
                     "client_id  kitchen   # trailing comment\n"
                     "username bob\n"
                     "password \"two words\"\n"
                     "cafile ca.crt\n"
                     "certfile /abs/client.crt\n"
                     "keyfile /abs/client.key\n"
                     "keepalive 30\n"
                     "reconnect on\n"
                     "clean_session off\n"
                     "will_topic dev/k/status\n"
                     "will_payload offline\n"
                     "will_qos 1\n"
                     "will_retain yes\n"
                     "subscribe a/# 1\n"
                     "subscribe b\n"));
    td_mqtt_config_t c;
    char err[96] = "";
    CHECK(td_mqtt_load_config(path, plain_resolve, NULL, &c, err, sizeof(err)));
    if (err[0])
        printf("config: %s\n", err);
    CHECK(!strcmp(c.host, "broker.example.com"));
    CHECK(c.tls && c.port == 0);                     /* 8883 chosen at connect */
    CHECK(!strcmp(c.client_id, "kitchen"));
    CHECK(!strcmp(c.pass, "two words"));
    CHECK(!strcmp(c.ca_file, "ca.crt"));              /* no folder in the path */
    CHECK(!strcmp(c.cert_file, "/abs/client.crt"));
    CHECK(c.keepalive == 30 && c.auto_reconnect && c.persistent);
    CHECK(!strcmp(c.will_topic, "dev/k/status") && c.will_qos == 1 && c.will_retain);
    CHECK_EQ(c.nsubs, 2);
    CHECK(!strcmp(c.sub_topic[0], "a/#") && c.sub_qos[0] == 1);
    CHECK(!strcmp(c.sub_topic[1], "b") && c.sub_qos[1] == 0);

    /* Relative paths are taken from the file's folder. */
    CHECK(write_text(path, "host h\ncafile certs/ca.pem\n"));
    char dirpath[64];
    snprintf(dirpath, sizeof(dirpath), "./%s", path);
    CHECK(td_mqtt_load_config(dirpath, plain_resolve, NULL, &c, err, sizeof(err)));
    CHECK(!strcmp(c.ca_file, "./certs/ca.pem"));

    /* Errors name the line. */
    CHECK(write_text(path, "host h\n\nfrobnicate 1\n"));
    CHECK(!td_mqtt_load_config(path, plain_resolve, NULL, &c, err, sizeof(err)));
    CHECK(strstr(err, "line 3") != NULL);
    CHECK(write_text(path, "host h\ncafile ../../etc/secret\n"));
    CHECK(!td_mqtt_load_config(path, plain_resolve, NULL, &c, err, sizeof(err)));
    CHECK(write_text(path, "host h\ncertfile c.pem\n"));
    CHECK(!td_mqtt_load_config(path, plain_resolve, NULL, &c, err, sizeof(err)));   /* keyfile missing */
    CHECK(!td_mqtt_load_config("no_such_file.conf", plain_resolve, NULL, &c, err, sizeof(err)));
    remove(path);

    td_mqtt_config_t b;
    memset(&b, 0, sizeof(b));
    CHECK(td_mqtt_parse_broker("mqtts://h.example:8884", &b) && b.tls && b.port == 8884);
    CHECK(td_mqtt_parse_broker("mqtt://10.0.0.2/", &b) && !b.tls && b.port == 0 && !strcmp(b.host, "10.0.0.2"));
    CHECK(td_mqtt_parse_broker("plainhost:1999", &b) && b.port == 1999);
    CHECK(!td_mqtt_parse_broker("ws://h", &b));
}

int main(void)
{
    test_config();
    test_crc();
    test_mqtt_length();
    test_topics();
    test_pdu();
    test_split();
    test_rtu();
    test_tcp();
    return TD_TEST_RESULT();
}
