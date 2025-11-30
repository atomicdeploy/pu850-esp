#pragma once

#include "../ASA0002E.h"

#define NO_CUT_HERE

extern "C" void crash_ets_uart_putc1(char c);

/*
extern "C" void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);
*/
