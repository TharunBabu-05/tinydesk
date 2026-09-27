/*
 * td_hal.h - the hardware abstraction layer.
 *
 * This is the only thing a port has to provide. The core never touches
 * hardware or operating-system APIs directly.
 */
#ifndef TD_HAL_H
#define TD_HAL_H

#include <stdint.h>

typedef struct td_hal {
    /* Return the next input byte (0..255), or -1 if none is waiting.
     * Must never block. */
    int (*read_byte)(void *ctx);

    /* Write up to len bytes. Returns the number of bytes accepted. May
     * block briefly (a few tens of ms) but must never block forever. */
    int (*write)(void *ctx, const uint8_t *buf, int len);

    /* Milliseconds since an arbitrary start point (wraps after ~49 days). */
    uint32_t (*millis)(void *ctx);

    /* Sleep or yield the CPU for about ms milliseconds. */
    void (*sleep_ms)(void *ctx, uint32_t ms);

    /* Port-private state passed back to every callback. */
    void *ctx;
} td_hal_t;

#endif /* TD_HAL_H */
