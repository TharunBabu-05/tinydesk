/*
 * td_tls.h - TLS client over a non-blocking td_sock socket (mbedTLS).
 *
 * Built when TD_HAVE_TLS is defined (ESP-IDF always; host builds when an
 * mbedTLS source tree is found). Without it, td_tls_available() is false
 * and td_tls_start() fails with a message.
 *
 * Certificates are PEM (or DER) file contents. Without a CA the system's
 * trusted roots are used: ESP-IDF's certificate bundle, the Windows ROOT
 * store, or the usual CA bundle files on Linux.
 */
#ifndef TD_TLS_H
#define TD_TLS_H

#include <stdbool.h>
#include <stddef.h>

#include "td_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct td_tls td_tls_t;

typedef struct
{
    const char *ca;
    size_t ca_len;     /* NULL: the system's roots */
    const char *cert;
    size_t cert_len;   /* client certificate (optional) */
    const char *key;
    size_t key_len;    /* its private key */
    const char *key_pass;                       /* for an encrypted key, or NULL */
    const char *server_name;                    /* SNI and name check */
    bool insecure;                              /* do not verify the server */
} td_tls_config_t;

bool td_tls_available(void);

/* Set up a client session on a connected socket (the socket stays owned by
 * the caller). NULL on failure, with a message in err. */
td_tls_t *td_tls_start(td_sock_t sock, const td_tls_config_t *cfg, char *err, size_t cap);

/* 1: done, 0: call again, -1: failed (message in err). */
int td_tls_handshake(td_tls_t *t, char *err, size_t cap);

/* Like td_sock_send / td_sock_recv: bytes, 0 would block, -1 error/closed. */
int td_tls_send(td_tls_t *t, const void *buf, int len);
int td_tls_recv(td_tls_t *t, void *buf, int cap);

/* "TLSv1.2 TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256" */
void td_tls_info(td_tls_t *t, char *buf, size_t cap);

void td_tls_free(td_tls_t *t);

/* Read a whole file (at most max bytes) into a NUL-terminated buffer the
 * caller frees. *len excludes the NUL. NULL with a message on failure. */
char *td_read_file(const char *path, size_t max, size_t *len, char *err, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
