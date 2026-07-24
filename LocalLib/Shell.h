#pragma once

#include "../ASA0002E.h"
#include <EscapeCodes.h>

#ifndef CMD_LEN
#define CMD_LEN 64
#endif

#ifndef SERIAL_SHELL_SERVICE_BUDGET
#define SERIAL_SHELL_SERVICE_BUDGET 64
#endif

#define KEY_BELL   0x07
#define KEY_ESCAPE 0x1B

#define CTRL_C     0x03
#define CTRL_L     0x0C
#define KEY_BKSP   0x08
#define KEY_TAB    0x09
#define KEY_LF     0x0A
#define KEY_CR     0x0D
#define KEY_ERASE  0x7F

#define ARR_UP     0x41
#define ARR_DOWN   0x42
#define ARR_RIGHT  0x43
#define ARR_LEFT   0x44
#define ARR_END    0x46
#define ARR_HOME   0x48

enum ShellTransport : U8 {
	ShellTransport_Serial = 0,
	ShellTransport_Telnet = 1,
};

enum ShellFlag : U8 {
	ShellFlag_Running = 1 << 0,
	ShellFlag_Escape = 1 << 1,
	ShellFlag_Extended = 1 << 2,
	ShellFlag_UpperCase = 1 << 3,
};

typedef size_t (*ShellWriteFunction)(
	void *context,
	const U8 *data,
	size_t size
);
typedef void (*ShellCloseFunction)(void *context);

struct ShellSession {
	U8 flags;
	U8 cursorPos;
	C8 command[CMD_LEN + 1];
	C8 escapeMode;
	C8 lastEscapeCharacter;
	bool lineEndingPending;
	bool connected;
	ShellTransport transport;
	void *context;
	ShellWriteFunction write;
	ShellCloseFunction close;
};

#include "Commands.h"

extern EscapeCodes ansi;
extern ShellSession SerialShellSession;
extern ShellSession TelnetShellSession;

/*
 * Commands.cpp predates transport-aware shell sessions. Keep its public
 * surface compatible while ShellExecutePrompt scopes this pointer to the
 * session that issued the command.
 */
extern C8 *command;

void ShellInitialize(
	ShellSession &session,
	ShellTransport transport,
	ShellWriteFunction write,
	ShellCloseFunction close = nullptr,
	void *context = nullptr
);
void ShellDisconnect(ShellSession &session);
void ShellExit(ShellSession &session);
void ShellService(ShellSession &session, U8 ch);
void ShellSend(ShellSession &session, U8 ch);
void ShellSend(ShellSession &session, const U8 *data, size_t size);

void SerialShell_Begin();
void SerialShell_Service(U8 budget = SERIAL_SHELL_SERVICE_BUDGET);
size_t SerialShellWrite(void *context, const U8 *data, size_t size);

void ShellSendCursor(ShellSession &session, U8 direction);
void ShellSendCursorMulti(ShellSession &session, U8 direction, U8 count);
void ShellShiftLeft(ShellSession &session);
void ShellShiftRight(ShellSession &session);
void ShellFixCursor(ShellSession &session, bool clearAfter);
void ShellNewPrompt(ShellSession &session, bool clearScreen);
void ShellRedrawPrompt(ShellSession &session);
void ShellExecutePrompt(ShellSession &session);
void ShellClearScreen(ShellSession &session);
void ShellBroadcastLine(const C8 *str);

U8 CompStr(const C8 *str1, const C8 *str2);
void String_ToShell(ShellSession &session, const C8 *str);
void Number_ToShell(ShellSession &session, S32 number);
void NewLine_ToShell(ShellSession &session, U8 count);
void String_NewLine_ToShell(ShellSession &session, const C8 *str);
void String_Num_NewLine_ToShell(
	ShellSession &session,
	const C8 *str,
	S32 number
);
void NumInsertInText_ToShell(
	ShellSession &session,
	const C8 *str,
	S32 number
);
void HexNumber_ToShell(ShellSession &session, U32 number);
bool NumberFromString(const C8 *str, S32 *number);

/*
 * Legacy command callbacks use these transport-neutral overloads. They write
 * only to the ShellSession currently executing that callback.
 */
void Shell_clearScreen();
void Shell_exit();
void newPrompt(bool clearScreen);
void String_ToShell(const C8 *str);
void Number_ToShell(S32 number);
void NewLine_ToShell(U8 count);
void String_NewLine_ToShell(const C8 *str);
void String_Num_NewLine_ToShell(const C8 *str, S32 number);
void NumInsertInText_ToShell(const C8 *str, S32 number);
void HexNumber_ToShell(U32 number);

inline C8 ToUpper(C8 ch)
{
	return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}
