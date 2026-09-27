/*
 * td_tls.c - TLS client on mbedTLS 3.x, non-blocking.
 */
#include "td_tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *td_read_file(const char *path, size_t max, size_t *len, char *err, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, cap, "cannot open %.60s", path);
        return NULL;
    }
    /* Only as much memory as the file needs: RAM is tight on the ESP32
     * (a certificate is usually 1-2 KB, not the 16 KB allowed). */
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || (size_t)size > max) {
        fclose(f);
        if (size < 0) snprintf(err, cap, "cannot read %.60s", path);
        else snprintf(err, cap, "%.50s is too big (over %u bytes)", path, (unsigned)max);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, cap, "not enough memory for %.50s (%ld bytes)", path, size);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

#ifndef TD_HAVE_TLS

bool td_tls_available(void) { return false; }

td_tls_t *td_tls_start(td_sock_t sock, const td_tls_config_t *cfg, char *err, size_t cap)
{
    (void)sock;
    (void)cfg;
    snprintf(err, cap, "TLS is not available in this build");
    return NULL;
}

int td_tls_handshake(td_tls_t *t, char *err, size_t cap) { (void)t; (void)err; (void)cap; return -1; }
int td_tls_send(td_tls_t *t, const void *buf, int len) { (void)t; (void)buf; (void)len; return -1; }
int td_tls_recv(td_tls_t *t, void *buf, int cap) { (void)t; (void)buf; (void)cap; return -1; }
void td_tls_info(td_tls_t *t, char *buf, size_t cap) { (void)t; if (cap) buf[0] = '\0'; }
void td_tls_free(td_tls_t *t) { (void)t; }

#else

#include "mbedtls/bignum.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#if defined(MBEDTLS_PSA_CRYPTO_C) && !defined(ESP_PLATFORM)
#include "psa/crypto.h"
#endif

#if defined(ESP_PLATFORM)
#include "esp_crt_bundle.h"
#elif defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

struct td_tls {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca, cert;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    td_sock_t sock;
    bool verify;
    bool has_cert;
};

bool td_tls_available(void) { return true; }

static void mbed_err(char *err, size_t cap, const char *what, int rc)
{
    if (rc == MBEDTLS_ERR_SSL_ALLOC_FAILED || rc == MBEDTLS_ERR_X509_ALLOC_FAILED || rc == MBEDTLS_ERR_PK_ALLOC_FAILED
#ifdef MBEDTLS_ERR_MPI_ALLOC_FAILED
        || rc == MBEDTLS_ERR_MPI_ALLOC_FAILED
#endif
    ) {
        snprintf(err, cap, "%s: not enough memory for TLS (close apps or stop a server, then retry)", what);
        return;
    }
    char m[96];
    mbedtls_strerror(rc, m, sizeof(m));
    snprintf(err, cap, "%s: %s", what, m);
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    td_tls_t *t = ctx;
    int n = td_sock_send(t->sock, buf, (int)len);
    if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return n == 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    td_tls_t *t = ctx;
    int n = td_sock_recv(t->sock, buf, (int)len);
    if (n < 0) return MBEDTLS_ERR_NET_CONN_RESET;
    return n == 0 ? MBEDTLS_ERR_SSL_WANT_READ : n;
}

/* PEM must be parsed with its NUL terminator; DER without. */
static int parse_crt(mbedtls_x509_crt *crt, const char *data, size_t len)
{
    bool pem = strstr(data, "-----BEGIN") != NULL;
    return mbedtls_x509_crt_parse(crt, (const unsigned char *)data, pem ? len + 1 : len);
}

/* The system's trusted roots. Returns false if there are none. */
static bool system_roots(td_tls_t *t)
{
#if defined(ESP_PLATFORM)
    return esp_crt_bundle_attach(&t->conf) == ESP_OK;
#elif defined(_WIN32)
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (!store) return false;
    int loaded = 0;
    PCCERT_CONTEXT c = NULL;
    while ((c = CertEnumCertificatesInStore(store, c)) != NULL)
        if (mbedtls_x509_crt_parse_der(&t->ca, c->pbCertEncoded, c->cbCertEncoded) == 0) loaded++;
    CertCloseStore(store, 0);
    if (loaded) mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
    return loaded > 0;
#else
    static const char *const files[] = {
        "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (mbedtls_x509_crt_parse_file(&t->ca, files[i]) >= 0 && t->ca.version) {
            mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
            return true;
        }
    }
    return false;
#endif
}

td_tls_t *td_tls_start(td_sock_t sock, const td_tls_config_t *cfg, char *err, size_t cap)
{
#if defined(MBEDTLS_PSA_CRYPTO_C) && !defined(ESP_PLATFORM)
    /* Host mbedTLS builds include TLS 1.3, which needs PSA set up. Not on the
     * ESP32: TLS 1.2 does not use it there, and linking it in would cost
     * 1.8 KB of static RAM that SSH needs at start-up. */
    psa_crypto_init();
#endif
    td_tls_t *t = calloc(1, sizeof(*t));
    if (!t) {
        snprintf(err, cap, "not enough memory for TLS");
        return NULL;
    }
    t->sock = sock;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_x509_crt_init(&t->ca);
    mbedtls_x509_crt_init(&t->cert);
    mbedtls_pk_init(&t->key);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);

    int rc = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                                   (const unsigned char *)"tinydesk-mqtt", 13);
    if (rc) {
        mbed_err(err, cap, "random seed", rc);
        goto fail;
    }
    rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) {
        mbed_err(err, cap, "TLS setup", rc);
        goto fail;
    }
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    /* TLS 1.2 everywhere: the ESP-IDF build has no TLS 1.3, and mbedTLS
     * 3.6.0's TLS 1.3 client mishandles "insecure" and some servers. */
    mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);

    t->verify = !cfg->insecure;
    mbedtls_ssl_conf_authmode(&t->conf, t->verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
    if (t->verify) {
        if (cfg->ca) {
            rc = parse_crt(&t->ca, cfg->ca, cfg->ca_len);
            if (rc < 0) {
                mbed_err(err, cap, "cafile", rc);
                goto fail;
            }
            mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
        } else if (!system_roots(t)) {
            snprintf(err, cap, "no trusted CA certificates here: set cafile (or insecure on)");
            goto fail;
        }
    }
    if (cfg->cert || cfg->key) {
        if (!cfg->cert || !cfg->key) {
            snprintf(err, cap, "certfile and keyfile go together");
            goto fail;
        }
        rc = parse_crt(&t->cert, cfg->cert, cfg->cert_len);
        if (rc < 0) {
            mbed_err(err, cap, "certfile", rc);
            goto fail;
        }
        bool pem = strstr(cfg->key, "-----BEGIN") != NULL;
        const char *pw = cfg->key_pass && cfg->key_pass[0] ? cfg->key_pass : NULL;
        rc = mbedtls_pk_parse_key(&t->key, (const unsigned char *)cfg->key, pem ? cfg->key_len + 1 : cfg->key_len,
                                  (const unsigned char *)pw, pw ? strlen(pw) : 0, mbedtls_ctr_drbg_random, &t->drbg);
        if (rc) {
            mbed_err(err, cap, pw ? "keyfile (wrong password?)" : "keyfile", rc);
            goto fail;
        }
        t->has_cert = true;
        rc = mbedtls_ssl_conf_own_cert(&t->conf, &t->cert, &t->key);
        if (rc) {
            mbed_err(err, cap, "client certificate", rc);
            goto fail;
        }
    }
    rc = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (rc) {
        mbed_err(err, cap, "TLS session", rc);
        goto fail;
    }
    if (cfg->server_name && cfg->server_name[0]) mbedtls_ssl_set_hostname(&t->ssl, cfg->server_name);
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
    return t;

fail:
    td_tls_free(t);
    return NULL;
}

int td_tls_handshake(td_tls_t *t, char *err, size_t cap)
{
    int rc = mbedtls_ssl_handshake(&t->ssl);
    if (rc == 0) return 1;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
    if (t->verify && flags && flags != (uint32_t)-1) {
        char why[160];
        mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
        for (char *p = why; *p; p++)
            if (*p == '\n') *p = (p[1] ? ';' : '\0');
        snprintf(err, cap, "server certificate rejected:%s%s", why,
                 (flags & (MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_EXPIRED)) ? " (is the clock set?)" : "");
    } else if (t->verify && (rc == MBEDTLS_ERR_X509_FATAL_ERROR || rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)) {
        /* ESP-IDF's bundle check fails this way for an unknown CA. */
        snprintf(err, cap, "server certificate not trusted: set cafile to the broker's CA (or tls_insecure on)");
    } else if (rc == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE && !t->has_cert) {
        snprintf(err, cap, "the broker refused the TLS connection (does it need certfile/keyfile?)");
    } else {
        mbed_err(err, cap, "TLS handshake", rc);
    }
    return -1;
}

int td_tls_send(td_tls_t *t, const void *buf, int len)
{
    int rc = mbedtls_ssl_write(&t->ssl, buf, (size_t)len);
    if (rc >= 0) return rc;
    return (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) ? 0 : -1;
}

int td_tls_recv(td_tls_t *t, void *buf, int cap)
{
    int rc = mbedtls_ssl_read(&t->ssl, buf, (size_t)cap);
    if (rc > 0) return rc;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;
#endif
    return -1;                                 /* closed or failed */
}

void td_tls_info(td_tls_t *t, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s %s%s", mbedtls_ssl_get_version(&t->ssl), mbedtls_ssl_get_ciphersuite(&t->ssl),
             t->verify ? "" : " (server not verified)");
}

void td_tls_free(td_tls_t *t)
{
    if (!t) return;
    mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_x509_crt_free(&t->cert);
    mbedtls_pk_free(&t->key);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    free(t);
}

#endif
