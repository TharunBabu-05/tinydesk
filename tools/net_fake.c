/*
 * net_fake.c - a pretend network for tdsim, so the Network app and the
 * taskbar indicator can be exercised without hardware. Operations finish
 * after a short delay, like the real ones.
 */
#include <stdio.h>
#include <string.h>

#include "net_fake.h"
#include "tinydesk/td.h"

static const td_wifi_ap_t s_air[] = {
    {"HomeNetwork", -48, true, true},
    {"Cafe Guest", -70, false, false},
    {"Neighbour-5G", -82, true, false},
};
#define AIR ((int)(sizeof(s_air) / sizeof(s_air[0])))

static td_wifi_ap_t s_known[AIR];
static bool s_up = true;
static char s_ssid[33] = "HomeNetwork";
static char s_msg[64];
static uint32_t s_busy_until;
static bool s_scan_pending;
static bool s_telnet = true;
static bool s_server_on[2] = {true, false};

static bool fake_server_status(int which, int *port, int *clients)
{
    if (port)
        *port = which == TD_SERVER_SSH ? 22 : 21;
    if (clients)
        *clients = 0;
    return s_server_on[which];
}

static bool fake_server_set(int which, bool on)
{
    s_server_on[which] = on;
    return true;
}

static bool busy(void)
{
    return (int32_t)(s_busy_until - td_millis()) > 0;
}

static void fake_status(td_net_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->wifi_up = s_up;
    snprintf(out->ssid, sizeof(out->ssid), "%s", s_ssid);
    out->rssi = -48;
    snprintf(out->wifi_ip, sizeof(out->wifi_ip), "192.168.1.42");
    out->busy = busy();
    snprintf(out->message, sizeof(out->message), "%s", s_msg);
}

static bool fake_scan(void)
{
    if (busy())
        return false;
    s_busy_until = td_millis() + 600;
    s_scan_pending = true;
    return true;
}

static int fake_results(td_wifi_ap_t *out, int max)
{
    if (busy())
        return -1;
    int n = AIR < max ? AIR : max;
    memcpy(out, s_air, sizeof(td_wifi_ap_t) * (size_t)n);
    for (int i = 0; i < n; i++)
        if (s_known[i].ssid[0])
            out[i].saved = true;
    s_scan_pending = false;
    return n;
}

static bool fake_connect(const char *ssid, const char *password)
{
    (void)password;
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    s_up = true;
    s_busy_until = td_millis() + 400;
    snprintf(s_msg, sizeof(s_msg), "Connected to %.32s", ssid);
    for (int i = 0; i < AIR; i++)
        if (strcmp(s_air[i].ssid, ssid) == 0)
            s_known[i] = s_air[i];
    return true;
}

static bool fake_disconnect(void)
{
    s_up = false;
    snprintf(s_msg, sizeof(s_msg), "Disconnected");
    return true;
}

static bool fake_forget(const char *ssid)
{
    for (int i = 0; i < AIR; i++)
        if (strcmp(s_air[i].ssid, ssid) == 0)
            memset(&s_known[i], 0, sizeof(s_known[i]));
    return true;
}

static bool telnet_get(void)
{
    return s_telnet;
}
static void telnet_set(bool on)
{
    s_telnet = on;
}
static const char *telnet_peer(void)
{
    return NULL;
}

static const td_net_ops_t s_ops = {
    .status = fake_status,
    .scan = fake_scan,
    .scan_results = fake_results,
    .connect = fake_connect,
    .disconnect = fake_disconnect,
    .forget = fake_forget,
    .server_status = fake_server_status,
    .server_set = fake_server_set,
    .telnet_enabled = telnet_get,
    .telnet_enable = telnet_set,
    .telnet_peer = telnet_peer,
};

const td_net_ops_t *td_net_fake(void)
{
    s_known[0] = s_air[0];
    return &s_ops;
}
