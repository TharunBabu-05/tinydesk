/*
 * main.c - tinydesk on the Windows console. Run it in Windows Terminal.
 */
#include "../common/td_host_hal.h"

int main(void)
{
    return td_host_main("Windows host");
}
