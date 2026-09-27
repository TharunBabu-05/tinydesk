/*
 * tdsh_win_commands.c - the Windows port's own TinyDesk Shell commands.
 *
 * The POSIX port registers ifconfig, ping, date, cal, tz, write, hostpath
 * and capabilities (ports/posix/tdsh_posix_commands.c in TinyDesk Shell),
 * written with getifaddrs(), posix_spawn() and termios. These are the same
 * commands for Windows: ifconfig and ping use the IP Helper API, so no
 * child process writes to the console behind the desktop's back.
 *
 * Built into the tdsh library with tdsh_win_compat.h force-included, so
 * printf() and fgets(stdin) are the shell session's streams.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tdsh.h"
#include "tdsh_platform_win.h"

static int usage(const char *name)
{
    const tdsh_command_t *c = tdsh_command_find(name);
    if (c) printf("usage: %s\n", c->usage);
    return 2;
}

/* ------------------------------------------------------------- ifconfig */

static int cmd_ifconfig(tdsh_session_t *s, int argc, char **argv)
{
    (void)s; (void)argv;
    if (argc != 1) return usage("ifconfig");
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; tries++) {
        free(list);
        list = malloc(size);
        if (!list) { puts("ifconfig: out of memory"); return 1; }
        rc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  NULL, list, &size);
    }
    if (rc != NO_ERROR) {
        printf("ifconfig: GetAdaptersAddresses failed (%lu)\n", (unsigned long)rc);
        free(list);
        return 1;
    }
    for (IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !a->FirstUnicastAddress) continue;
        char name[128];
        if (!WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), NULL, NULL))
            snprintf(name, sizeof(name), "%s", a->AdapterName);
        printf("%s\n", name);
        if (a->PhysicalAddressLength == 6) {
            const BYTE *m = a->PhysicalAddress;
            printf("    ether %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
        }
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
            int fam = u->Address.lpSockaddr->sa_family;
            char text[INET6_ADDRSTRLEN] = "";
            const void *src = fam == AF_INET ? (const void *)&((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr
                                             : (const void *)&((struct sockaddr_in6 *)u->Address.lpSockaddr)->sin6_addr;
            if (!inet_ntop(fam, src, text, sizeof(text))) continue;
            printf("    %s %s/%u\n", fam == AF_INET ? "inet " : "inet6", text, (unsigned)u->OnLinkPrefixLength);
        }
    }
    free(list);
    return 0;
}

/* ----------------------------------------------------------------- ping */

static int cmd_ping(tdsh_session_t *s, int argc, char **argv)
{
    (void)s;
    int count = 4;
    const char *host = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            count = atoi(argv[++i]);
            if (count < 1 || count > 100) { puts("ping: -c takes 1 to 100"); return 2; }
        } else if (!host) {
            host = argv[i];
        } else {
            return usage("ping");
        }
    }
    if (!host) return usage("ping");

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { puts("ping: network not available"); return 1; }
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        printf("ping: cannot resolve %s\n", host);
        WSACleanup();
        return 1;
    }
    IPAddr dest = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, text, sizeof(text));
    freeaddrinfo(res);

    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) { puts("ping: cannot open ICMP"); WSACleanup(); return 1; }
    char payload[32];
    memset(payload, 'a', sizeof(payload));
    unsigned char reply[sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 64];
    printf("PING %s (%s): %u data bytes\n", host, text, (unsigned)sizeof(payload));
    int received = 0;
    unsigned long min_ms = (unsigned long)-1, max_ms = 0, sum_ms = 0;
    for (int i = 0; i < count; i++) {
        DWORD n = IcmpSendEcho(icmp, dest, payload, sizeof(payload), NULL, reply, sizeof(reply), 1000);
        ICMP_ECHO_REPLY *r = (ICMP_ECHO_REPLY *)reply;
        if (n > 0 && r->Status == IP_SUCCESS) {
            received++;
            unsigned long ms = r->RoundTripTime;
            if (ms < min_ms) min_ms = ms;
            if (ms > max_ms) max_ms = ms;
            sum_ms += ms;
            printf("%u bytes from %s: seq=%d ttl=%u time=%lu ms\n", (unsigned)r->DataSize, text, i + 1,
                   (unsigned)r->Options.Ttl, ms);
        } else {
            printf("no reply from %s: seq=%d\n", text, i + 1);
        }
        if (i + 1 < count) Sleep(1000);
    }
    IcmpCloseHandle(icmp);
    WSACleanup();
    printf("--- %s: %d sent, %d received, %d%% loss", host, count, received, (count - received) * 100 / count);
    if (received) printf(", time min/avg/max %lu/%lu/%lu ms", min_ms, sum_ms / (unsigned long)received, max_ms);
    printf("\n");
    return received ? 0 : 1;
}

/* ---------------------------------------------------- date, cal and tz */

/* The PC's own offset from UTC, used until the user sets one with tz
 * (the desktop's taskbar clock does the same). */
static long pc_offset(void)
{
    time_t now = time(NULL);
    struct tm g;
    gmtime_s(&g, &now);
    g.tm_isdst = -1;
    return (long)difftime(now, mktime(&g));    /* as ports/common/host_main.c */
}

static bool tz_path(tdsh_session_t *s, char *real, size_t cap)
{
    return tdsh_path_to_real(s, "~/.tdsh_tz", real, cap, NULL, 0) == 0;
}

static long tz_offset(tdsh_session_t *s)
{
    char real[TDSH_MAX_REAL_PATH];
    long v = pc_offset();
    if (!tz_path(s, real, sizeof(real))) return v;
    FILE *f = fopen(real, "r");
    if (!f) return v;
    if (fscanf(f, "%ld", &v) != 1) v = pc_offset();
    fclose(f);
    return v;
}

static int cmd_tz(tdsh_session_t *s, int argc, char **argv)
{
    if (argc > 2) return usage("tz");
    if (argc == 2) {
        int sign = 1, h = 0, m = 0;
        const char *p = argv[1];
        if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
        if (sscanf(p, "%d:%d", &h, &m) != 2 || h > 14 || m > 59) { puts("tz: expected [+|-]HH:MM"); return 1; }
        char real[TDSH_MAX_REAL_PATH];
        if (!tz_path(s, real, sizeof(real))) return 1;
        FILE *f = fopen(real, "w");
        if (!f) { printf("tz: %s\n", strerror(errno)); return 1; }
        fprintf(f, "%ld\n", sign * (h * 3600L + m * 60L));
        fclose(f);
    }
    long off = tz_offset(s), a = labs(off);
    printf("UTC%c%02ld:%02ld\n", off < 0 ? '-' : '+', a / 3600, (a % 3600) / 60);
    return 0;
}

static int local_tm(tdsh_session_t *s, struct tm *out)
{
    time_t t = time(NULL) + (time_t)tz_offset(s);
    return gmtime_s(out, &t) == 0 ? 0 : 1;
}

static int cmd_date(tdsh_session_t *s, int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return usage("date");
    struct tm tm;
    if (local_tm(s, &tm)) return 1;
    char b[96];
    strftime(b, sizeof(b), "%a %d-%m-%Y %H:%M:%S", &tm);
    printf("%s\n", b);
    return 0;
}

static int cmd_cal(tdsh_session_t *s, int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return usage("cal");
    struct tm tm;
    if (local_tm(s, &tm)) return 1;
    int y = tm.tm_year + 1900, m = tm.tm_mon + 1;
    /* weekday of the 1st: Zeller-style from the known weekday of today */
    int first_wday = ((tm.tm_wday - (tm.tm_mday - 1) % 7) + 7) % 7;
    int days = 31;
    if (m == 4 || m == 6 || m == 9 || m == 11) days = 30;
    else if (m == 2) days = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    static const char *mn[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};
    printf("     %s %d\nSu Mo Tu We Th Fr Sa\n", mn[m - 1], y);
    for (int i = 0; i < first_wday; i++) printf("   ");
    for (int d = 1; d <= days; d++) printf("%2d%c", d, ((first_wday + d) % 7) == 0 ? '\n' : ' ');
    if ((first_wday + days) % 7) putchar('\n');
    return 0;
}

/* ------------------------------------------------------ write, hostpath */

static int cmd_hostpath(tdsh_session_t *s, int argc, char **argv)
{
    if (argc > 2) return usage("hostpath");
    char real[TDSH_MAX_REAL_PATH], logical[TDSH_MAX_PATH];
    const char *p = argc == 2 ? argv[1] : s->cwd;
    int rc = tdsh_path_to_real(s, p, real, sizeof(real), logical, sizeof(logical));
    if (rc) { printf("hostpath: unable to resolve '%s' (%d)\n", p, rc); return 1; }
    printf("%s -> %s\n", logical, real);
    return 0;
}

static int cmd_write(tdsh_session_t *s, int argc, char **argv)
{
    if (argc < 2) return usage("write");
    char real[TDSH_MAX_REAL_PATH], logical[TDSH_MAX_PATH];
    if (tdsh_path_to_real(s, argv[1], real, sizeof(real), logical, sizeof(logical)) != 0) {
        puts("write: invalid path");
        return 1;
    }
    if (argc > 2) {
        FILE *f = fopen(real, "w");
        if (!f) { printf("write: %s: %s\n", logical, strerror(errno)); return 1; }
        for (int i = 2; i < argc; i++) fprintf(f, "%s%s", argv[i], i + 1 < argc ? " " : "");
        fputc('\n', f);
        fclose(f);
        return 0;
    }
    if (!s->interactive) { puts("write: interactive terminal required"); return 1; }
    printf("TinyDesk Shell line writer: %s\nEnter lines; a line containing only .save finishes. .quit discards.\n", logical);
    char tmp[TDSH_MAX_REAL_PATH + 16];
    snprintf(tmp, sizeof(tmp), "%s.tmp", real);
    FILE *out = fopen(tmp, "w");
    if (!out) { printf("write: %s\n", strerror(errno)); return 1; }
    char line[TDSH_MAX_LINE + 2];
    for (;;) {
        fputs("write> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { fclose(out); remove(tmp); return 1; }
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, ".quit")) { fclose(out); remove(tmp); puts("Changes discarded."); return 0; }
        if (!strcmp(line, ".save")) {
            fclose(out);
            remove(real);                    /* rename() does not replace on Windows */
            if (rename(tmp, real) != 0) { printf("write: save: %s\n", strerror(errno)); remove(tmp); return 1; }
            printf("Saved %s\n", logical);
            return 0;
        }
        fprintf(out, "%s\n", line);
    }
}

static int cmd_caps(tdsh_session_t *s, int argc, char **argv)
{
    (void)s; (void)argv;
    if (argc != 1) return usage("capabilities");
    puts("Windows capabilities:\n"
         "  portable shell/uScript/filesystem: yes\n"
         "  isolated TinyDesk Shell filesystem: yes (tinydesk_fs next to the program)\n"
         "  write: yes (nano: use the Editor app)\n"
         "  date/tz/cal: yes\n"
         "  ifconfig/ping: yes (IPv4 ping)\n"
         "  persistent TinyDesk Shell users: no (ESP-IDF port only)\n"
         "  ESP Wi-Fi/W6100/GPIO/hwtest/OTA, SSH/FTP/SMB servers: ESP-IDF port only");
    return 0;
}

static const tdsh_command_t s_cmds[] = {
    {"ifconfig", "ifconfig", "Show the PC's network interfaces", cmd_ifconfig, 0},
    {"ping", "ping [-c count] <host/address>", "Send ICMP echo requests (IPv4)", cmd_ping, 0},
    {"tz", "tz [[+|-]HH:MM]", "Show or set per-user timezone offset", cmd_tz, 0},
    {"date", "date", "Show current date/time using TinyDesk Shell timezone", cmd_date, 0},
    {"cal", "cal", "Print current month calendar", cmd_cal, 0},
    {"write", "write <file> [text ...]", "Line writer or direct file writer", cmd_write, 0},
    {"hostpath", "hostpath [path]", "Show real host path for a TinyDesk Shell path", cmd_hostpath, 0},
    {"capabilities", "capabilities", "Show capabilities of this platform port", cmd_caps, 0},
};

int tdsh_win_register_commands(void)
{
    return tdsh_register_commands(s_cmds, sizeof(s_cmds) / sizeof(s_cmds[0]));
}
