/*
 * rs485.c - the board's RS-485 lines as Modbus RTU lines (`rtu1`, `rtu2`),
 * each on its own UART in RS-485 half-duplex mode (the UART drives DE
 * through its RTS pin, so nothing is toggled by hand).
 *
 * The wiring comes from the board configuration (tdsh_board.h,
 * the Board configuration page of the docs):
 *
 *   rs485.1.uart = <UART number>      rs485.2.uart = ...
 *   rs485.1.tx   = <GPIO>             rs485.2.tx   = ...
 *   rs485.1.rx   = <GPIO>             rs485.2.rx   = ...
 *   rs485.1.de   = <GPIO>             rs485.2.de   = ...
 *
 * A line is available when all four keys are set. A UART used here must
 * not be the one the desktop or a console uses (UART0 on the classic
 * ESP32). A driver is installed only while its line is in use: td_modbus
 * closes it after 15 s without requests, so `hwtest rs485` can use the
 * pins again. A line that is not open holds DE low (listening), so it
 * never drives its bus.
 */
#include "rs485.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "tdsh_board.h"

#define RX_BUF 256          /* a Modbus frame is at most 256 bytes */
#define LINES 2

typedef struct {
    int uart, tx, rx, de;
    char name[80];
} channel_t;

static channel_t s_ch[LINES];
static int s_lines;         /* configured lines, 1..LINES; 0: none */
static int s_open = -1;     /* line in use (1 or 2), -1: none */

static bool line_ok(int port) { return port >= 1 && port <= LINES && s_ch[port - 1].uart >= 0; }

/* A closed line listens: its DE pin is driven low. */
static void hold_de_low(const channel_t *c)
{
    gpio_reset_pin(c->de);
    gpio_set_direction(c->de, GPIO_MODE_OUTPUT);
    gpio_set_level(c->de, 0);
}

static void rs485_close(void)
{
    if (s_open >= 1) {
        const channel_t *c = &s_ch[s_open - 1];
        uart_driver_delete(c->uart);
        gpio_reset_pin(c->tx);
        gpio_reset_pin(c->rx);
        hold_de_low(c);
    }
    s_open = -1;
}

static bool rs485_open(int port, uint32_t baud, char parity, int stop_bits, char *err, size_t cap)
{
    if (!line_ok(port)) {
        snprintf(err, cap, "RS-485 line %d is not configured (board keys rs485.%d.*)", port, port);
        return false;
    }
    rs485_close();
    const channel_t *c = &s_ch[port - 1];
    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = parity == 'E' ? UART_PARITY_EVEN : parity == 'O' ? UART_PARITY_ODD : UART_PARITY_DISABLE,
        .stop_bits = stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_driver_install(c->uart, RX_BUF, 0, 0, NULL, 0);
    if (e == ESP_OK) e = uart_param_config(c->uart, &cfg);
    if (e == ESP_OK) e = uart_set_pin(c->uart, c->tx, c->rx, c->de, UART_PIN_NO_CHANGE);
    if (e == ESP_OK) e = uart_set_mode(c->uart, UART_MODE_RS485_HALF_DUPLEX);
    if (e != ESP_OK) {
        uart_driver_delete(c->uart);
        hold_de_low(c);
        snprintf(err, cap, "%s: %s (in use by hwtest?)", c->name, esp_err_to_name(e));
        return false;
    }
    s_open = port;
    return true;
}

static int rs485_write(const uint8_t *buf, int len)
{
    if (s_open < 1) return -1;
    int n = uart_write_bytes(s_ch[s_open - 1].uart, buf, (size_t)len);
    return n < 0 ? -1 : n;
}

static int rs485_read(uint8_t *buf, int cap)
{
    if (s_open < 1) return -1;
    uart_port_t u = s_ch[s_open - 1].uart;
    size_t avail = 0;
    if (uart_get_buffered_data_len(u, &avail) != ESP_OK || avail == 0) return 0;
    if (avail > (size_t)cap) avail = (size_t)cap;
    int n = uart_read_bytes(u, buf, (uint32_t)avail, 0);
    return n < 0 ? -1 : n;
}

static const char *rs485_name(int port) { return line_ok(port) ? s_ch[port - 1].name : "not configured"; }

static td_mb_serial_t s_serial = {
    .ports = 0,
    .name = rs485_name,
    .open = rs485_open,
    .write = rs485_write,
    .read = rs485_read,
    .close = rs485_close,
};

const td_mb_serial_t *rs485_serial(void)
{
    static bool loaded;
    if (!loaded) {
        loaded = true;
        for (int i = 0; i < LINES; i++) {
            char key[24];
            channel_t *c = &s_ch[i];
            snprintf(key, sizeof(key), "rs485.%d.uart", i + 1);
            c->uart = tdsh_board_int(key, -1);
            snprintf(key, sizeof(key), "rs485.%d.tx", i + 1);
            c->tx = tdsh_board_int(key, -1);
            snprintf(key, sizeof(key), "rs485.%d.rx", i + 1);
            c->rx = tdsh_board_int(key, -1);
            snprintf(key, sizeof(key), "rs485.%d.de", i + 1);
            c->de = tdsh_board_int(key, -1);
            if (c->uart < 0 || c->uart >= UART_NUM_MAX || c->tx < 0 || c->rx < 0 || c->de < 0) {
                c->uart = -1;
                continue;
            }
            snprintf(c->name, sizeof(c->name), "RS485-%d (UART%d: TX%d RX%d DE%d)", i + 1, c->uart, c->tx, c->rx,
                     c->de);
            hold_de_low(c);                     /* listen until used */
            s_lines = i + 1;
        }
        s_serial.ports = s_lines;
    }
    return s_lines ? &s_serial : NULL;
}
