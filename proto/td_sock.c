/*
 * td_sock.c - non-blocking TCP sockets, clock, sleep and lock for Winsock,
 * POSIX and ESP-IDF (lwIP).
 */
#if !defined(_WIN32) && !defined(ESP_PLATFORM)
#define _POSIX_C_SOURCE 200809L
#endif

#include "td_sock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SOCK_ERR()     WSAGetLastError()
#define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
#define IN_PROGRESS(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
typedef int socklen_t_;
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCK_ERR()     errno
#define WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define IN_PROGRESS(e) ((e) == EINPROGRESS || (e) == EAGAIN)
typedef socklen_t socklen_t_;
#endif

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#elif !defined(_WIN32)
#include <pthread.h>
#include <time.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ------------------------------------------------------------ helpers */

static bool net_init(void)
{
#if defined(_WIN32)
    static bool done;
    if (!done)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        done = true;
    }
#endif
    return true;
}

static void set_err(char *err, size_t cap, const char *what, int code)
{
    if (!err || !cap)
        return;
#if defined(_WIN32)
    snprintf(err, cap, "%s (error %d)", what, code);
#else
    snprintf(err, cap, "%s: %s", what, strerror(code));
#endif
}

static bool set_nonblocking(td_sock_t s)
{
#if defined(_WIN32)
    u_long on = 1;
    return ioctlsocket((SOCKET)s, FIONBIO, &on) == 0;
#else
    int flags = fcntl((int)s, F_GETFL, 0);
    return flags >= 0 && fcntl((int)s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void td_sock_close(td_sock_t s)
{
    if (s == TD_SOCK_INVALID)
        return;
#if defined(_WIN32)
    closesocket((SOCKET)s);
#else
    close((int)s);
#endif
}

/* ------------------------------------------------------------ address */

bool td_split_host_port(const char *text, char *host, size_t host_cap, uint16_t *port, uint16_t default_port)
{
    if (!text || !text[0] || !host_cap)
        return false;
    const char *colon = NULL;
    const char *h = text;
    size_t hlen;
    if (text[0] == '[')
    {                        /* [v6]:port */
        const char *end = strchr(text, ']');
        if (!end)
            return false;
        h = text + 1;
        hlen = (size_t)(end - h);
        if (end[1] == ':')
            colon = end + 1;
        else if (end[1])
            return false;
    }
    else
    {
        colon = strrchr(text, ':');
        if (colon && strchr(text, ':') != colon)
            colon = NULL;   /* bare IPv6 */
        hlen = colon ? (size_t)(colon - text) : strlen(text);
    }
    if (hlen == 0 || hlen >= host_cap)
        return false;
    memcpy(host, h, hlen);
    host[hlen] = '\0';
    *port = default_port;
    if (colon)
    {
        char *end = NULL;
        long p = strtol(colon + 1, &end, 10);
        if (!end || *end || p < 1 || p > 65535)
            return false;
        *port = (uint16_t)p;
    }
    return true;
}

bool td_sock_resolve(const char *host, uint16_t port, td_addr_t *out, char *err, size_t cap)
{
    if (!net_init())
    {
        set_err(err, cap, "network unavailable", 0);
        return false;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char service[8];
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    int rc = getaddrinfo(host, service, &hints, &res);
    if (rc != 0 || !res)
    {
        if (err && cap)
            snprintf(err, cap, "cannot resolve %.40s", host);
        return false;
    }
    /* Prefer IPv4 (lwIP builds often have no IPv6 routing). */
    struct addrinfo *pick = res;
    for (struct addrinfo *a = res; a; a = a->ai_next)
        if (a->ai_family == AF_INET)
        {
            pick = a;
            break;
        }
    if ((size_t)pick->ai_addrlen > sizeof(out->data))
    {
        freeaddrinfo(res);
        if (err && cap)
            snprintf(err, cap, "unsupported address");
        return false;
    }
    memcpy(out->data, pick->ai_addr, (size_t)pick->ai_addrlen);
    out->len = (int)pick->ai_addrlen;
    freeaddrinfo(res);
    return true;
}

void td_addr_text(const td_addr_t *addr, char *buf, size_t cap)
{
    const struct sockaddr *sa = (const struct sockaddr *)addr->data;
    char ip[48] = "?";
    unsigned port = 0;
    if (sa->sa_family == AF_INET)
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr->data;
        inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip));
        port = ntohs(in->sin_port);
        snprintf(buf, cap, "%s:%u", ip, port);
    }
#ifdef AF_INET6
    else if (sa->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)addr->data;
        inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip));
        port = ntohs(in6->sin6_port);
        snprintf(buf, cap, "[%s]:%u", ip, port);
    }
#endif
    else
    {
        snprintf(buf, cap, "?");
    }
}

/* ------------------------------------------------------------ connect */

td_sock_t td_sock_connect_start(const td_addr_t *addr, char *err, size_t cap)
{
    if (!net_init())
        return TD_SOCK_INVALID;
    const struct sockaddr *sa = (const struct sockaddr *)addr->data;
    td_sock_t s = (td_sock_t)socket(sa->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == TD_SOCK_INVALID || s < 0)
    {
        set_err(err, cap, "socket", SOCK_ERR());
        return TD_SOCK_INVALID;
    }
    if (!set_nonblocking(s))
    {
        set_err(err, cap, "non-blocking mode", SOCK_ERR());
        td_sock_close(s);
        return TD_SOCK_INVALID;
    }
    int one = 1;
    setsockopt((int)s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    if (connect((int)s, sa, (socklen_t_)addr->len) != 0)
    {
        int e = SOCK_ERR();
        if (!IN_PROGRESS(e))
        {
            set_err(err, cap, "connect", e);
            td_sock_close(s);
            return TD_SOCK_INVALID;
        }
    }
    return s;
}

int td_sock_connect_poll(td_sock_t s, char *err, size_t cap)
{
    fd_set wr, ex;
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET((int)s, &wr);
    FD_SET((int)s, &ex);
    struct timeval tv = {0, 0};
    int n = select((int)s + 1, NULL, &wr, &ex, &tv);
    if (n < 0)
    {
        set_err(err, cap, "select", SOCK_ERR());
        return -1;
    }
    if (n == 0)
        return 0;
    int so = 0;
    socklen_t_ len = sizeof(so);
    if (getsockopt((int)s, SOL_SOCKET, SO_ERROR, (char *)&so, &len) != 0)
        so = SOCK_ERR();
    if (so != 0 || FD_ISSET((int)s, &ex))
    {
#if defined(_WIN32)
        set_err(err, cap, so == WSAECONNREFUSED ? "connection refused" : "connect failed", so);
#else
        set_err(err, cap, "connect", so ? so : ECONNREFUSED);
#endif
        return -1;
    }
    return 1;
}

/* --------------------------------------------------------------- data */

int td_sock_send(td_sock_t s, const void *buf, int len)
{
    int n = (int)send((int)s, (const char *)buf, len, MSG_NOSIGNAL);
    if (n >= 0)
        return n;
    return WOULD_BLOCK(SOCK_ERR()) ? 0 : -1;
}

int td_sock_recv(td_sock_t s, void *buf, int cap)
{
    int n = (int)recv((int)s, (char *)buf, cap, 0);
    if (n > 0)
        return n;
    if (n == 0)
        return -1;                    /* closed by the peer */
    return WOULD_BLOCK(SOCK_ERR()) ? 0 : -1;
}

/* ------------------------------------------------------------- server */

td_sock_t td_sock_listen(uint16_t port, char *err, size_t cap)
{
    if (!net_init())
        return TD_SOCK_INVALID;
    td_sock_t s = (td_sock_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == TD_SOCK_INVALID || s < 0)
    {
        set_err(err, cap, "socket", SOCK_ERR());
        return TD_SOCK_INVALID;
    }
    int one = 1;
    setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind((int)s, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        set_err(err, cap, "bind", SOCK_ERR());
        td_sock_close(s);
        return TD_SOCK_INVALID;
    }
    if (listen((int)s, 2) != 0 || !set_nonblocking(s))
    {
        set_err(err, cap, "listen", SOCK_ERR());
        td_sock_close(s);
        return TD_SOCK_INVALID;
    }
    return s;
}

td_sock_t td_sock_accept(td_sock_t listener, char *peer, size_t peer_cap)
{
    struct sockaddr_storage ss;
    socklen_t_ len = sizeof(ss);
    td_sock_t c = (td_sock_t)accept((int)listener, (struct sockaddr *)&ss, &len);
    if (c == TD_SOCK_INVALID || c < 0)
        return TD_SOCK_INVALID;
    set_nonblocking(c);
    if (peer && peer_cap)
    {
        td_addr_t a;
        a.len = (int)len;
        memcpy(a.data, &ss, len < (socklen_t_)sizeof(a.data) ? (size_t)len : sizeof(a.data));
        td_addr_text(&a, peer, peer_cap);
    }
    return c;
}

/* ------------------------------------------------------ clock, lock */

#if defined(ESP_PLATFORM)

uint32_t td_proto_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}
void td_proto_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_lock_mux = portMUX_INITIALIZER_UNLOCKED;

void td_proto_lock(void)
{
    if (!s_lock)
    {
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        taskENTER_CRITICAL(&s_lock_mux);
        if (!s_lock)
        {
            s_lock = m;
            m = NULL;
        }
        taskEXIT_CRITICAL(&s_lock_mux);
        if (m)
            vSemaphoreDelete(m);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void td_proto_unlock(void)
{
    xSemaphoreGive(s_lock);
}

#elif defined(_WIN32)

uint32_t td_proto_millis(void)
{
    return (uint32_t)GetTickCount64();
}
void td_proto_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_cs;

static BOOL CALLBACK init_cs(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&s_cs);
    return TRUE;
}

void td_proto_lock(void)
{
    InitOnceExecuteOnce(&s_once, init_cs, NULL, NULL);
    EnterCriticalSection(&s_cs);
}

void td_proto_unlock(void)
{
    LeaveCriticalSection(&s_cs);
}

#else

uint32_t td_proto_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000));
}

void td_proto_sleep_ms(uint32_t ms)
{
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
void td_proto_lock(void)
{
    pthread_mutex_lock(&s_mutex);
}
void td_proto_unlock(void)
{
    pthread_mutex_unlock(&s_mutex);
}

#endif
