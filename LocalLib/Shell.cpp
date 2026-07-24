#include "Shell.h"

EscapeCodes ansi;
ShellSession SerialShellSession = {};
ShellSession TelnetShellSession = {};

static C8 detachedCommand[CMD_LEN + 1] = {};
C8 *command = detachedCommand;

static ShellSession *activeOutputSession = nullptr;

#include "Commands.cpp"

class ShellOutputScope {
public:
	explicit ShellOutputScope(ShellSession &session)
		: previousSession(activeOutputSession), previousCommand(command)
	{
		activeOutputSession = &session;
		command = session.command;
	}

	~ShellOutputScope()
	{
		activeOutputSession = previousSession;
		command = previousCommand;
	}

private:
	ShellSession *previousSession;
	C8 *previousCommand;
};

static bool ShellHasFlag(const ShellSession &session, ShellFlag flag)
{
	return (session.flags & flag) != 0;
}

static void ShellSetFlag(
	ShellSession &session,
	ShellFlag flag,
	bool value
)
{
	if (value)
		session.flags |= flag;
	else
		session.flags &= ~flag;
}

static void ShellResetInput(ShellSession &session)
{
	const U8 persistentFlags = session.flags & ShellFlag_UpperCase;
	session.flags = persistentFlags;
	session.cursorPos = 0;
	session.escapeMode = 0;
	session.lastEscapeCharacter = 0;
	memset(session.command, Null_, sizeof(session.command));
}

static void ShellSubmitLine(ShellSession &session)
{
	NewLine_ToShell(session, 1);

	if (strlen(session.command) > 0)
		ShellExecutePrompt(session);

	if (session.connected)
		ShellNewPrompt(session, false);
}

static void ShellWritePromptPrefix(ShellSession &session)
{
	String_ToShell(session, ansi.setFG(ANSI_BRIGHT_YELLOW).c_str());
	String_ToShell(session, ansi.bold("").c_str());
	String_ToShell(session, "> ");
	String_ToShell(session, ansi.reset().c_str());
}

static void ShellWriteEventLine(ShellSession &session, const C8 *str)
{
	if (!session.connected || str == nullptr)
		return;

	if (ShellHasFlag(session, ShellFlag_Running)) {
		String_ToShell(session, "\r\e[2K");
		String_NewLine_ToShell(session, str);
		ShellRedrawPrompt(session);
		return;
	}

	String_NewLine_ToShell(session, str);
}

void ShellInitialize(
	ShellSession &session,
	ShellTransport transport,
	ShellWriteFunction write,
	ShellCloseFunction close,
	void *context
)
{
	memset(&session, 0, sizeof(session));
	session.transport = transport;
	session.context = context;
	session.write = write;
	session.close = close;
	session.connected = true;
}

void ShellDisconnect(ShellSession &session)
{
	ShellResetInput(session);
	session.lineEndingPending = false;
	session.connected = false;
}

void ShellExit(ShellSession &session)
{
	if (session.close != nullptr)
		session.close(session.context);
}

size_t SerialShellWrite(void *, const U8 *data, size_t size)
{
	if (data == nullptr || size == 0)
		return 0;

	return Serial.write(data, size);
}

void SerialShell_Begin()
{
	ShellInitialize(
		SerialShellSession,
		ShellTransport_Serial,
		SerialShellWrite
	);

	ShellClearScreen(SerialShellSession);
	String_ToShell(
		SerialShellSession,
		ansi.setFG(ANSI_BRIGHT_GREEN).c_str()
	);

	String welcome = "Welcome to " FW_PRODUCT_NAME;
	const String hostname = WiFi.hostname();
	if (hostname.length() > 0)
		welcome += " (" + hostname + ")";
	welcome += "!";

	String_NewLine_ToShell(SerialShellSession, welcome.c_str());
	String_ToShell(SerialShellSession, ansi.reset().c_str());
	ShellNewPrompt(SerialShellSession, false);
}

void SerialShell_Service(U8 budget)
{
	while (
		budget-- > 0 &&
		SerialShellSession.connected &&
		Serial.available() > 0
	) {
		const int value = Serial.read();
		if (value >= 0)
			ShellService(
				SerialShellSession,
				static_cast<U8>(value)
			);
	}
}

void ShellSend(ShellSession &session, U8 ch)
{
	ShellSend(session, &ch, 1);
}

void ShellSend(
	ShellSession &session,
	const U8 *data,
	size_t size
)
{
	if (
		!session.connected ||
		session.write == nullptr ||
		data == nullptr ||
		size == 0
	)
		return;

	session.write(session.context, data, size);
}

void ShellService(ShellSession &session, U8 ch)
{
	if (!session.connected)
		return;

	/*
	 * NVT sends CR LF or CR NUL. Serial terminals commonly send CR LF.
	 * Submit once for CR and consume its optional trailing byte, even when it
	 * arrives in a later ESPTelnet callback.
	 */
	if (session.lineEndingPending && (ch == KEY_LF || ch == 0)) {
		session.lineEndingPending = false;
		return;
	}

	if (ch == KEY_CR || ch == KEY_LF) {
		session.lineEndingPending = (ch == KEY_CR);
		ShellSubmitLine(session);
		return;
	}

	session.lineEndingPending = false;

	if (ShellHasFlag(session, ShellFlag_Escape)) {
		if (session.escapeMode == 0) {
			switch (ch) {
				case '[':
				case 'O':
					session.escapeMode = static_cast<C8>(ch);
					break;

				default:
					/*
					 * A few serial terminals omit '[' or 'O' from their
					 * cursor-key sequences.
					 */
					if (
						ch == ARR_LEFT || ch == ARR_RIGHT ||
						ch == ARR_UP || ch == ARR_DOWN ||
						ch == ARR_HOME || ch == ARR_END
					) {
						session.escapeMode = 'O';
						ShellService(session, ch);
						return;
					}

					ShellSetFlag(
						session,
						ShellFlag_Escape,
						false
					);
					session.escapeMode = 0;
					ShellSend(session, KEY_BELL);
					break;
			}
			return;
		}

		if (session.lastEscapeCharacter == 0) {
			if (
				session.escapeMode == '[' ||
				session.escapeMode == 'O'
			) {
				if (ch == ARR_LEFT) {
					if (session.cursorPos > 0) {
						session.cursorPos--;
						ShellSendCursor(session, ch);
					} else {
						ShellSend(session, KEY_BELL);
					}
				} else if (ch == ARR_RIGHT) {
					if (session.cursorPos < strlen(session.command)) {
						session.cursorPos++;
						ShellSendCursor(session, ch);
					} else {
						ShellSend(session, KEY_BELL);
					}
				} else if (ch == ARR_HOME) {
					if (session.cursorPos > 0) {
						ShellSendCursorMulti(
							session,
							ARR_LEFT,
							session.cursorPos
						);
						session.cursorPos = 0;
						ShellFixCursor(session, false);
					}
				} else if (ch == ARR_END) {
					const U8 length = strlen(session.command);
					if (session.cursorPos < length) {
						ShellSendCursorMulti(
							session,
							ARR_RIGHT,
							length - session.cursorPos
						);
						session.cursorPos = length;
						ShellFixCursor(session, false);
					}
				} else if (ch >= '0' && ch <= '9') {
					session.lastEscapeCharacter =
						static_cast<C8>(ch);
					return;
				} else {
					ShellSend(session, KEY_BELL);
				}
			}

			ShellSetFlag(session, ShellFlag_Escape, false);
			session.escapeMode = 0;
			return;
		}

		if (
			session.lastEscapeCharacter >= '0' &&
			session.lastEscapeCharacter <= '9'
		) {
			const C8 attribute = session.lastEscapeCharacter;

			if (ch == '~') {
				switch (attribute) {
					case '3':
						if (
							session.cursorPos <
							strlen(session.command)
						) {
							ShellShiftLeft(session);
							ShellFixCursor(session, true);
						} else {
							ShellSend(session, KEY_BELL);
						}
						break;

					case '2':
						// Input is always inserted.
						break;

					case '1':
					case '4':
						session.escapeMode = 'O';
						session.lastEscapeCharacter = 0;
						ShellService(
							session,
							attribute == '1'
								? ARR_HOME
								: ARR_END
						);
						return;

					default:
						ShellSend(session, KEY_BELL);
						break;
				}
			} else {
				ShellSend(session, KEY_BELL);
			}

			ShellSetFlag(session, ShellFlag_Escape, false);
			session.escapeMode = 0;
			session.lastEscapeCharacter = 0;
		}

		return;
	}

	session.lastEscapeCharacter = 0;

	if (ch == KEY_ESCAPE) {
		ShellSetFlag(session, ShellFlag_Escape, true);
		session.escapeMode = 0;
		return;
	}

	if (ch == CTRL_C) {
		NewLine_ToShell(session, 1);
		ShellNewPrompt(session, false);
		return;
	}

	if (ch == KEY_TAB) {
		ShellSend(session, KEY_BELL);
		return;
	}

	if (ch == KEY_BKSP || ch == KEY_ERASE) {
		if (session.cursorPos > 0) {
			session.cursorPos--;
			ShellShiftLeft(session);
			ShellSendCursor(session, ARR_LEFT);
			ShellFixCursor(session, true);
		} else {
			ShellSend(session, KEY_BELL);
		}
		return;
	}

	if (ch == CTRL_L) {
		C8 savedCommand[sizeof(session.command)];
		const U8 savedCursor = session.cursorPos;
		const bool upperCase =
			ShellHasFlag(session, ShellFlag_UpperCase);

		memcpy(savedCommand, session.command, sizeof(savedCommand));
		ShellNewPrompt(session, true);
		memcpy(session.command, savedCommand, sizeof(session.command));
		ShellSetFlag(session, ShellFlag_UpperCase, upperCase);
		String_ToShell(session, session.command);
		session.cursorPos = savedCursor;
		ShellSendCursorMulti(
			session,
			ARR_LEFT,
			strlen(session.command) - session.cursorPos
		);
		return;
	}

	if (ch < 32 || ch >= 127) {
		ShellSend(session, KEY_BELL);
		return;
	}

	if (strlen(session.command) >= CMD_LEN) {
		ShellSend(session, KEY_BELL);
		return;
	}

	ShellShiftRight(session);
	session.command[session.cursorPos++] = static_cast<C8>(ch);
	ShellSend(
		session,
		ShellHasFlag(session, ShellFlag_UpperCase)
			? ToUpper(static_cast<C8>(ch))
			: ch
	);
	ShellFixCursor(session, false);
}

void ShellSendCursor(ShellSession &session, U8 direction)
{
	if (direction == 0)
		return;

	const U8 sequence[] = {KEY_ESCAPE, '[', direction};
	ShellSend(session, sequence, sizeof(sequence));
}

void ShellSendCursorMulti(
	ShellSession &session,
	U8 direction,
	U8 count
)
{
	if (direction == 0 || count == 0)
		return;

	ShellSend(session, KEY_ESCAPE);
	ShellSend(session, '[');
	Number_ToShell(session, count);
	ShellSend(session, direction);
}

void ShellShiftLeft(ShellSession &session)
{
	for (U8 i = session.cursorPos; i < CMD_LEN; ++i)
		session.command[i] = session.command[i + 1];
}

void ShellShiftRight(ShellSession &session)
{
	for (U8 i = CMD_LEN; i > session.cursorPos; --i)
		session.command[i] = session.command[i - 1];
}

void ShellFixCursor(ShellSession &session, bool clearAfter)
{
	const U8 length = strlen(session.command);

	if (session.cursorPos != length) {
		ShellSendCursor(session, 's');
		for (U8 i = session.cursorPos; i < length; ++i) {
			ShellSend(
				session,
				ShellHasFlag(session, ShellFlag_UpperCase)
					? ToUpper(session.command[i])
					: session.command[i]
			);
		}
		String_ToShell(session, "\e[J");
		ShellSendCursor(session, 'u');
	} else if (clearAfter) {
		String_ToShell(session, "\e[K");
	}
}

void ShellClearScreen(ShellSession &session)
{
	String_ToShell(session, "\e[H\e[2J");
}

void ShellNewPrompt(ShellSession &session, bool clearScreen)
{
	if (!session.connected)
		return;

	if (clearScreen)
		ShellClearScreen(session);

	ShellResetInput(session);
	ShellWritePromptPrefix(session);
	ShellSetFlag(session, ShellFlag_Running, true);
}

void ShellRedrawPrompt(ShellSession &session)
{
	if (!session.connected)
		return;

	ShellWritePromptPrefix(session);

	const U8 length = strlen(session.command);
	for (U8 i = 0; i < length; ++i) {
		ShellSend(
			session,
			ShellHasFlag(session, ShellFlag_UpperCase)
				? ToUpper(session.command[i])
				: session.command[i]
		);
	}

	if (session.cursorPos < length) {
		ShellSendCursorMulti(
			session,
			ARR_LEFT,
			length - session.cursorPos
		);
	}
}

U8 CompStr(const C8 *str1, const C8 *str2)
{
	if (str1 == nullptr || str2 == nullptr)
		return NotOK_;

	const size_t length = strlen(str2);

	if (strlen(str1) != length)
		return NotOK_;

	for (size_t i = 0; i < length; ++i) {
		if (ToUpper(str1[i]) != ToUpper(str2[i]))
			return NotOK_;
	}

	return OK_;
}

void ShellExecutePrompt(ShellSession &session)
{
	ShellOutputScope outputScope(session);
	U8 i;
	C8 *args = session.command + CMD_LEN;
	session.cursorPos = strlen(session.command);

	for (i = 0; i < CMD_LEN; ++i) {
		if (
			session.command[i] == ' ' ||
			session.command[i] == '\t'
		) {
			session.command[i] = Null_;
			args = &session.command[i] + 1;
			break;
		}
	}

	for (i = 0; i < count_of(commandMap); ++i) {
		if (
			CompStr(commandMap[i].command, session.command) ==
			OK_
		) {
			commandMap[i].function(args);
			return;
		}
	}

	if (
		CompStr(session.command, "clear") == OK_ ||
		CompStr(session.command, "cls") == OK_
	) {
		ShellClearScreen(session);
	} else if (CompStr(session.command, "exit") == OK_) {
		ShellExit(session);
	} else if (CompStr(session.command, "beep") == OK_) {
		if (args[0] == 0)
			args[0] = 1;
		Request_Beep(args[0]);
	} else if (CompStr(session.command, "hex") == OK_) {
		S32 number;

		if (NumberFromString(args, &number) == NotOK_) {
			String_NewLine_ToShell(
				session,
				"Invalid number."
			);
			return;
		}

		HexNumber_ToShell(session, static_cast<U32>(number));
		NewLine_ToShell(session, 1);
	} else if (CompStr(session.command, "upper") == OK_) {
		ShellSetFlag(session, ShellFlag_UpperCase, true);
	} else if (CompStr(session.command, "noupper") == OK_) {
		ShellSetFlag(session, ShellFlag_UpperCase, false);
	} else if (CompStr(session.command, "echo") == OK_) {
		String_NewLine_ToShell(session, args);
	} else if (
		CompStr(session.command, "get") == OK_ ||
		CompStr(session.command, "set") == OK_
	) {
		const bool isGet = ToUpper(session.command[0]) == 'G';
		C8 *value = args;

		for (i = 0; i < strlen(args); ++i) {
			if (args[i] == ' ' || args[i] == '\t') {
				args[i] = Null_;
				value = &args[i] + 1;
				break;
			}
		}

		if (args == value && !isGet) {
			String_NewLine_ToShell(
				session,
				"No value specified."
			);
			return;
		}

		const U8 id = getDeviceParameterIdByName(args);

		if (id == 0xff) {
			String_NewLine_ToShell(
				session,
				isGet
					? "Parameter not found."
					: "Bad parameter specified."
			);
			return;
		}

		if (
			!isGet &&
			(DeviceParameters[id].type & FLAG_READONLY)
		) {
			String_ToShell(
				session,
				ansi.setFG(ANSI_BRIGHT_YELLOW).c_str()
			);
			String_NewLine_ToShell(
				session,
				"Parameter is read-only."
			);
			String_ToShell(session, ansi.reset().c_str());
			return;
		}

		const U8 result = isGet
			? DeviceParameters[id].get(args)
			: DeviceParameters[id].set(value);

		if (result == OK_) {
			if (isGet) {
				String_NewLine_ToShell(session, args);
			} else {
				String_ToShell(
					session,
					ansi.setFG(ANSI_BRIGHT_GREEN).c_str()
				);
				String_NewLine_ToShell(session, "Success");
				String_ToShell(session, ansi.reset().c_str());
			}
		} else {
			String_ToShell(
				session,
				ansi.setFG(ANSI_BRIGHT_RED).c_str()
			);
			String_NewLine_ToShell(session, "Failed");
			String_ToShell(session, ansi.reset().c_str());
		}
	} else {
		String_NewLine_ToShell(session, "Command not found.");
	}
}

void String_ToShell(ShellSession &session, const C8 *str)
{
	if (str == nullptr)
		return;

	ShellSend(
		session,
		reinterpret_cast<const U8 *>(str),
		strlen(str)
	);
}

void Number_ToShell(ShellSession &session, S32 number)
{
	C8 digits[12];
	U8 length = 0;
	U32 magnitude;

	if (number < 0) {
		ShellSend(session, '-');
		magnitude = static_cast<U32>(-(number + 1)) + 1;
	} else {
		magnitude = static_cast<U32>(number);
	}

	if (magnitude == 0) {
		ShellSend(session, '0');
		return;
	}

	while (magnitude > 0 && length < sizeof(digits)) {
		digits[length++] =
			static_cast<C8>('0' + (magnitude % 10));
		magnitude /= 10;
	}

	while (length > 0)
		ShellSend(session, digits[--length]);
}

void NewLine_ToShell(ShellSession &session, U8 count)
{
	while (count-- > 0)
		String_ToShell(session, "\r\n");
}

void String_NewLine_ToShell(
	ShellSession &session,
	const C8 *str
)
{
	String_ToShell(session, str);
	NewLine_ToShell(session, 1);
}

void String_Num_NewLine_ToShell(
	ShellSession &session,
	const C8 *str,
	S32 number
)
{
	String_ToShell(session, str);
	Number_ToShell(session, number);
	NewLine_ToShell(session, 1);
}

void NumInsertInText_ToShell(
	ShellSession &session,
	const C8 *str,
	S32 number
)
{
	if (str == nullptr)
		return;

	while (*str != Null_) {
		if (*str == '%')
			Number_ToShell(session, number);
		else
			ShellSend(session, static_cast<U8>(*str));

		++str;
	}
}

void HexNumber_ToShell(ShellSession &session, U32 number)
{
	static const C8 hex[] = "0123456789ABCDEF";
	C8 result[10] = {Null_};
	U8 length = 0;

	if (number == 0) {
		result[length++] = '0';
	} else {
		while (number != 0 && length < sizeof(result) - 1) {
			result[length++] = hex[number & 0x0F];
			number >>= 4;
		}
	}

	if ((length % 2) != 0 && length < sizeof(result) - 1)
		result[length++] = '0';

	for (U8 i = 0; i < length / 2; ++i) {
		const C8 ch = result[i];
		result[i] = result[length - i - 1];
		result[length - i - 1] = ch;
	}

	ShellSend(
		session,
		reinterpret_cast<const U8 *>(result),
		length
	);
}

bool NumberFromString(const C8 *str, S32 *number)
{
	S32 value = 0;
	U8 i = 0;
	U8 negative = 0;
	C8 ch;

	if (str == nullptr || number == nullptr)
		return NotOK_;

	while ((ch = str[i++]) > 0) {
		if (ch == ' ' || ch == '\t' || ch == ',')
			continue;

		if (ch == '-') {
			if (negative > 0)
				return NotOK_;

			negative = 1;
			continue;
		}

		if (ch >= '0' && ch <= '9') {
			value = (value * 10) + (ch - '0');
			continue;
		}

		return NotOK_;
	}

	if (negative != 0)
		value = -value;

	*number = value;
	return OK_;
}

void ShellBroadcastLine(const C8 *str)
{
	ShellWriteEventLine(SerialShellSession, str);
	ShellWriteEventLine(TelnetShellSession, str);
}

static ShellSession *LegacyShellSession()
{
	return activeOutputSession;
}

void Shell_clearScreen()
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		ShellClearScreen(*session);
}

void Shell_exit()
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		ShellExit(*session);
}

void newPrompt(bool clearScreen)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		ShellNewPrompt(*session, clearScreen);
}

void String_ToShell(const C8 *str)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		String_ToShell(*session, str);
}

void Number_ToShell(S32 number)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		Number_ToShell(*session, number);
}

void NewLine_ToShell(U8 count)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		NewLine_ToShell(*session, count);
}

void String_NewLine_ToShell(const C8 *str)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		String_NewLine_ToShell(*session, str);
}

void String_Num_NewLine_ToShell(const C8 *str, S32 number)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		String_Num_NewLine_ToShell(*session, str, number);
}

void NumInsertInText_ToShell(const C8 *str, S32 number)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		NumInsertInText_ToShell(*session, str, number);
}

void HexNumber_ToShell(U32 number)
{
	ShellSession *session = LegacyShellSession();
	if (session != nullptr)
		HexNumber_ToShell(*session, number);
}
