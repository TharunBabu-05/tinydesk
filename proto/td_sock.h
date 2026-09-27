/*
 * td_sock.h - the little bit of platform glue the protocol library needs:
 * non-blocking TCP sockets, a clock, a sleep and one lock.
 *
 * One implementation (td_sock.c) covers Winsock, POSIX and ESP-IDF's lwIP.
 * Everything here returns at once except td_sock_resolve() (DNS) and
 * td_proto_sleep_ms().
 */
#ifndef TD_SOCK_H
#define TD_SOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef intptr_t td_sock_t;
#define TD_SOCK_INVALID ((td_sock_t)-1)

/* A resolved address (IPv4 or IPv6), kept so a reconnect needs no DNS. */
typedef struct {
    uint8_t data[28];   /* a struct sockaddr_in / sockaddr_in6 */
    int len;
} td_addr_t;

/* Resolve host (name or literal) and port. May block for DNS. Returns false
 * with a message in err. */
bool td_sock_resolve(const char *host, uint16_t port, td_addr_t *out, char *err, size_t cap);

/* Start a non-blocking connect. */
td_sock_t td_sock_connect_start(const td_addr_t *addr, char *err, size_t cap);

/* 1: connected, 0: still connecting, -1: failed (message in err). */
int td_sock_connect_poll(td_sock_t s, char *err, size_t cap);

/* Bytes sent (0: would block), or -1 on error. */
int td_sock_send(td_sock_t s, const void *buf, int len);

/* Bytes received (0: nothing yet), or -1 when closed or failed. */
int td_sock_recv(td_sock_t s, void *buf, int cap);

void td_sock_close(td_sock_t s);

/* A non-blocking listening socket on all interfaces. */
td_sock_t td_sock_listen(uint16_t port, char *err, size_t cap);

/* A new connection, or TD_SOCK_INVALID when none is waiting. */
td_sock_t td_sock_accept(td_sock_t listener, char *peer, size_t peer_cap);

/* "a.b.c.d:port" of an address. */
void td_addr_text(const td_addr_t *addr, char *buf, size_t cap);

/* --- platform helpers ---------------------------------------------- */

uint32_t td_proto_millis(void);
void td_proto_sleep_ms(uint32_t ms);

/* One process-wide lock for the protocol state, shared by the desktop's
 * main loop and shell commands running in other tasks. Not recursive. */
void td_proto_lock(void);
void td_proto_unlock(void);

/* Parse "host", "host:port" or "[v6]:port". Returns false if it is not
 * usable (the port stays default_port when none is given). */
bool td_split_host_port(const char *text, char *host, size_t host_cap, uint16_t *port, uint16_t default_port);

#ifdef __cplusplus
}
#endif

#endif
