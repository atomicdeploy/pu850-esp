#pragma once

#include "CrashHandler.h"

// Override the crash dump printer to inhibit output to UART
extern "C" void crash_ets_uart_putc1(C8 c) {
	// ets_uart_putc1(c);
}

// NOTE: Custom crash callback is not currently implemented.
// The function below provides a template for future implementation if needed.
// The callback must be kept quick and concise to execute before hardware WDT kicks in.
/*
extern "C" void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end)
{
	// Uptime until crash
	const uint32_t timestamp = millis();
	// crashInfo.write(SAVE_CRASH_CRASH_TIME, timestamp);

	// Write reset info
	// crashInfo.write(SAVE_CRASH_RESTART_REASON, rst_info->reason);
	// crashInfo.write(SAVE_CRASH_EXCEPTION_CAUSE, rst_info->exccause);

	// Write epc1, epc2, epc3, excvaddr, and depc
	// crashInfo.write(SAVE_CRASH_EPC1, rst_info->epc1);
	// crashInfo.write(SAVE_CRASH_EPC2, rst_info->epc2);
	// crashInfo.write(SAVE_CRASH_EPC3, rst_info->epc3);
	// crashInfo.write(SAVE_CRASH_EXCVADDR, rst_info->excvaddr);
	// crashInfo.write(SAVE_CRASH_DEPC, rst_info->depc);

	// Determine offset based on reset reason
	uint32_t offset = 0;
	if (rst_info->reason == REASON_SOFT_WDT_RST) {
		offset = 0x1b0;
	} else if (rst_info->reason == REASON_EXCEPTION_RST) {
		offset = 0x1a0;
	} else if (rst_info->reason == REASON_WDT_RST) {
		offset = 0x10;
	}

	// Check for free space for crash data
	// byte crashCounter = crashInfo.read(_offset + SAVE_CRASH_COUNTER);
	// int16_t writeFrom = (crashCounter == 0) ? SAVE_CRASH_DATA_SETS : crashInfo.get(_offset + SAVE_CRASH_WRITE_FROM);

	// if (writeFrom + SAVE_CRASH_STACK_TRACE > _size) {
	// 	return; // Not enough space to save stack trace
	// }

	// Increment crash counter and write it
	// crashInfo.write(_offset + SAVE_CRASH_COUNTER, ++crashCounter);
	// writeFrom += _offset;

	// Stack pointer and context structure
	register uint32_t sp asm("a1");
	cont_t g_cont __attribute__ ((aligned (16)));

	// Stack dump
	uint32_t cont_stack_start = (uint32_t) &(g_cont.stack);
	uint32_t cont_stack_end = (uint32_t) g_cont.stack_end;

	// Write stack trace header
	// crashInfo.write(">>>stack>>>\n", strlen(">>>stack>>>\n"));

	// Collect stack trace
	for (uint32_t pos = stack; pos < stack_end; pos += 0x10) {
		uint32_t* values = (uint32_t*)(pos);
		// Rough indicator: stack frames usually have SP saved as the second word
		const bool looksLikeStackFrame = (values[2] == pos + 0x10);

		// Write stack address and values to crashInfo
		// char tmpBuffer[100];
		// sprintf(tmpBuffer, "%08x: %08x %08x %08x %08x %c\n", pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame) ? '<' : ' ');
		// crashInfo.write(tmpBuffer, strlen(tmpBuffer));
	}

	// crashInfo.write("<<<stack<<<\n\n", strlen("<<<stack<<<\n\n"));

	// Update write position for next crash data
	// crashInfo.write(_offset + SAVE_CRASH_WRITE_FROM, writeFrom);
}
*/
