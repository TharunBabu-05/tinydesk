/*
 * main.c - tinydesk in a Linux / macOS terminal.
 */
#include "../common/td_host_hal.h"

int main(void)
{
#ifdef __APPLE__
    return td_host_main("macOS host");
#else
    return td_host_main("Linux host");
#endif
}
