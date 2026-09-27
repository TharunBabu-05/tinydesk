/*
 * td_modbus.h - Modbus client (master) over TCP or RTU, and a Modbus TCP
 * server (slave) with its own data tables.
 *
 * Function codes: 01 read coils, 02 read discrete inputs, 03 read holding
 * registers, 04 read input registers, 05 write coil, 06 write register,
 * 15 write coils, 16 write registers.
 *
 * Like td_mqtt, everything is non-blocking and driven by td_mb_poll(), and
 * shared by the Modbus app and the `modbus` shell command. The client runs
 * one request at a time. Memory is allocated when first used.
 */
#ifndef TD_MODBUS_H
#define TD_MODBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TD_MB_TCP_PORT 502
#define TD_MB_MAX_READ 125        /* registers per read (bits: 2000) */
#define TD_MB_MAX_WRITE 64        /* values per write request */
#define TD_MB_TABLE_SIZE 128      /* server: entries per table */

/* Tables, named by their read function code. */
typedef enum {
    TD_MB_COILS = 1,              /* 0x, read/write bits */
    TD_MB_DISCRETE = 2,           /* 1x, read-only bits */
    TD_MB_HOLDING = 3,            /* 4x, read/write registers */
    TD_MB_INPUT = 4,              /* 3x, read-only registers */
} td_mb_table_t;

/* Serial lines for Modbus RTU (provided by the port; optional). Lines
 * are numbered from 1; one is open at a time. */
typedef struct {
    int ports;                    /* how many lines (rtu1, rtu2, ...) */
    const char *(*name)(int port);   /* e.g. "RS485-1 (UART1: TX16 RX17 DE18)" */
    bool (*open)(int port, uint32_t baud, char parity, int stop_bits, char *err, size_t cap);
    int (*write)(const uint8_t *buf, int len);    /* bytes written, -1 error */
    int (*read)(uint8_t *buf, int cap);           /* non-blocking */
    void (*close)(void);
} td_mb_serial_t;

void td_mb_set_serial(const td_mb_serial_t *serial);
const td_mb_serial_t *td_mb_serial(void);

/* ------------------------------------------------------------- client */

typedef struct {
    char target[64];              /* "host[:port]" or "rtu[N][:baud[:8N1|8E1|8O1]]" */
    uint8_t unit;                 /* slave / unit id (0..247; TCP often 1 or 255) */
    uint8_t fc;                   /* function code */
    uint16_t addr;                /* 0-based */
    uint16_t count;               /* reads; writes use nvalues */
    uint16_t nvalues;
    uint16_t values[TD_MB_MAX_WRITE];
} td_mb_request_t;

typedef struct {
    int status;                   /* 0 ok, >0 Modbus exception, <0 error */
    char text[72];                /* human-readable outcome */
    uint8_t fc;
    uint16_t addr;
    uint16_t count;               /* values read */
    uint16_t values[TD_MB_MAX_READ];   /* registers, or bits as 0/1 when count <= 125 */
    uint32_t ms;                  /* round trip */
} td_mb_result_t;

/* Build a request from text (shared by the shell and the app).
 * table: "co|coils", "di|discrete", "hr|holding", "ir|input". */
bool td_mb_parse_table(const char *name, td_mb_table_t *out);
const char *td_mb_table_name(td_mb_table_t t);
const char *td_mb_exception_text(int code);

/* Queue a request. Returns a ticket (> 0), or 0 with a message in err
 * (busy, bad request, cannot resolve...). May block for DNS. */
int td_mb_submit(const td_mb_request_t *req, char *err, size_t cap);

/* True once ticket has finished; copies the result. */
bool td_mb_result(int ticket, td_mb_result_t *out);
bool td_mb_busy(void);

/* Convenience for blocking callers in another task (the shell): submit and
 * wait, driving td_mb_poll() meanwhile. */
bool td_mb_transact(const td_mb_request_t *req, td_mb_result_t *out, char *err, size_t cap);

/* ------------------------------------------------------------- server */

bool td_mb_server_start(uint16_t port, char *err, size_t cap);
void td_mb_server_stop(void);
bool td_mb_server_status(uint16_t *port, int *clients, uint32_t *requests);
/* Read or write server table entries (bits as 0/1). Returns entries done. */
int td_mb_server_get(td_mb_table_t t, uint16_t addr, uint16_t count, uint16_t *out);
int td_mb_server_set(td_mb_table_t t, uint16_t addr, uint16_t count, const uint16_t *in);

/* Drive the client and the server. */
void td_mb_poll(void);

/* --- exposed for tests ------------------------------------------------ */

uint16_t td_mb_crc16(const uint8_t *p, int len);

typedef struct {
    uint8_t coils[TD_MB_TABLE_SIZE / 8];
    uint8_t discrete[TD_MB_TABLE_SIZE / 8];
    uint16_t holding[TD_MB_TABLE_SIZE];
    uint16_t input[TD_MB_TABLE_SIZE];
} td_mb_tables_t;

/* Answer one request PDU (function code first) from tables. Returns the
 * response PDU length (an exception response when needed). */
int td_mb_serve_pdu(td_mb_tables_t *t, const uint8_t *req, int len, uint8_t *resp);

/* Request PDU for req (function code first); returns its length or -1. */
int td_mb_build_pdu(const td_mb_request_t *req, uint8_t *pdu);

/* Decode a response PDU into res. */
void td_mb_decode_pdu(const td_mb_request_t *req, const uint8_t *pdu, int len, td_mb_result_t *res);

#ifdef __cplusplus
}
#endif

#endif
