#include "Shell.h"
#include "Commands.cpp"

void ShellService(C8 ch)
{
	/** If we are currently processing an escape sequence */
	if (IsEscFlag) {

		// Which escape command are we decoding
		if (escMode == 0) {
			switch (ch) {
				case '[':
				case 'O':
					escMode = ch;
					break;

				default:
					// Special case for non VT100-compliant arrow codes
					if ( ch == ARR_LEFT || ch == ARR_RIGHT
					  || ch == ARR_UP   || ch == ARR_DOWN
					  || ch == ARR_HOME || ch == ARR_END )
					{
						escMode = 'O';
						ShellService(ch);
						return;
					}
				
					EscFlag_No;
					escMode = 0;
					// Received escape sequence is invalid
					Shell_Send(KEY_BELL);
					break;
			}
		}

		// Parse escape sequence attributes
		else if (lastCharEsc == 0) {

			// We have a complete escape command
			if (escMode == '[' || escMode == 'O') {

				if ( ch == ARR_LEFT ) {
					if ( Shell_cursorPos > 0 ) {
						Shell_cursorPos--;
						sendCursor(ch);
					} else
					Shell_Send( KEY_BELL );
				} else

				if ( ch == ARR_RIGHT ) {
					if ( Shell_cursorPos < strlen(command) ) {
						Shell_cursorPos++;
						sendCursor(ch);
					} else
					Shell_Send( KEY_BELL );
				} else

				if ( ch == ARR_HOME ) {
					if ( Shell_cursorPos > 0 ) {
						sendCursorMulti(ARR_LEFT, Shell_cursorPos);
						Shell_cursorPos = 0;
						fixPos(0);
					}
				} else

				if ( ch == ARR_END ) {
					if ( Shell_cursorPos < strlen(command) ) {
						sendCursorMulti(ARR_RIGHT, strlen(command) - Shell_cursorPos);
						Shell_cursorPos = strlen(command);
						fixPos(0);
					}
				} else

				// Multi-character escape code sequence
				if ( ch >= '0' && ch <= '9' ) {
					lastCharEsc = ch;
					return;
				} else

				{
					// Received escape sequence is unknown
					Shell_Send( KEY_BELL );
				}

			}

			// Reset the escape sequence
			EscFlag_No;
			escMode = 0;

		}

		// Parse multi-character escape code sequence
		else if (lastCharEsc >= '0' && lastCharEsc <= '9') {

			if ( ch == 0x7E ) { // ~
				switch (lastCharEsc)
				{
					case '3': // Delete
					{
						if ( Shell_cursorPos < strlen(command) ) {
							shiftToLeft();
							fixPos(1);
						} else
							Shell_Send( KEY_BELL );
					}
					break;
					
					case '2': // Insert
						// no support yet
						break;
					
					case '1': // Home
					case '4': // End
						escMode = 'O';
						lastCharEsc = 0;
						ShellService(ch == '1' ? ARR_HOME : ARR_END);
						return;
				}
			} else

			{
				// Received escape sequence is unknown
				Shell_Send( KEY_BELL );
			}

			// Reset the escape sequence
			EscFlag_No;
			escMode = 0;
			lastCharEsc = 0;
		}

	}

	else {
		lastCharEsc = 0;

		// Handle Escape Key sequence
		if ( ch == KEY_ESCAPE ) {
			EscFlag_Yes;
			escMode = 0;
		} else

		// Handle CTRL-C key code
		if ( ch == CTRL_C ) {
			NewLine_ToShell(1);
			newPrompt(0);
		} else

		// Handle Tab
		if ( ch == KEY_TAB ) {
			// Not supported yet
			Shell_Send( KEY_BELL );
		} else

		// Handle Carriage Return
		if ( ch == KEY_RETURN ) { } else

		// Handle New line
		if ( ch == KEY_ENTER ) {
			NewLine_ToShell(1);
			if ( strlen(command) > 0 )
				execPrompt();
			newPrompt(0);
		} else

		// Handle Backspace
		if ( ch == KEY_BKSP || ch == KEY_ERASE ) {
			if (Shell_cursorPos > 0 ) {
				Shell_cursorPos--;

				shiftToLeft();
				sendCursor(ARR_LEFT);

				fixPos(1);
			} else
			Shell_Send( KEY_BELL );
		} else

		// Handle CTRL+L
		if ( ch == CTRL_L ) {
			C8 str[sizeof(command)];
			U8 oldPos = Shell_cursorPos;
			memcpy(str, command, sizeof(command));
			newPrompt(1);
			memcpy(command, str, sizeof(command));
			String_ToShell(command);
			Shell_cursorPos = oldPos;
			sendCursorMulti(ARR_LEFT, strlen(command) - Shell_cursorPos);
		} else

		/*
		// Emulate CoolTerm-style arrow keys
		if ( ch >= 0x1E && ch <= 0x1C )
		{
			escMode = 'O';
			ShellService(ch + 0x23); // map to ARR_* macros
			return;
		} else
			
		// Emulate CoolTerm-style HOME and END keys
		if ( ch == SOH || ch == EOT )
		{
			escMode = 'O';
			ShellService(ch == SOH ? ARR_HOME : ARR_END);
			return;
		}
		*/

		// Handle all other control characters
		if ( ch < 32 || ch >= 127 ) {
			// Print the ch as HEX to Serial
			// Serial.print("0x");
			// Serial.println(ch, HEX);
			// String_ToShell("Unexpected control character: ");
			// HexNumber_ToShell(ch);
			// NewLine_ToShell(1);

			Shell_Send( KEY_BELL );
		} else

		// Handle all printable keys
		{
			if ( strlen(command) < CMD_LEN ) {
				shiftToRight();

				command[Shell_cursorPos++] = ch;
				Shell_Send( (IsUpperCaseCMD) ? ToUpper(ch) : ch );

				fixPos(0);
			} else
			Shell_Send( KEY_BELL ); // Command length overflow
		}

	}

}

void sendCursor(U8 direction) {
	if (direction > 0) {
		Shell_Send( KEY_ESCAPE );
		Shell_Send( '[' );
		Shell_Send( direction );
	}
}

void sendCursorMulti(U8 direction, U8 num) {
	if (direction > 0 && num > 0) {
		Shell_Send( KEY_ESCAPE );
		Shell_Send( '[' );
		Number_ToShell(num);
		Shell_Send( direction );
	}
}

void shiftToLeft() {
	U8 i;

	for (i = Shell_cursorPos; i<CMD_LEN; i++) {
		command[i] = command[i+1];
	}
}

void shiftToRight() {
	U8 i;

	for (i = CMD_LEN; i>Shell_cursorPos; i--) {
		command[i] = command[i-1];
	}
}

void fixPos(bool clearAfter) {
	if ( Shell_cursorPos != strlen(command) ) {
		sendCursor('s'); // Tell the terminal to save the current position
		for (U8 i = Shell_cursorPos; i<strlen(command); i++)
			Shell_Send( (IsUpperCaseCMD) ? ToUpper(command[i]) : command[i] );
		Shell_Send( KEY_ESCAPE );
		String_ToShell( "[J" );
		sendCursor('u'); // Tell the terminal to restore the position
	}
	else if (clearAfter) {
		Shell_Send( KEY_ESCAPE );
		String_ToShell( "[K" );
	}
}

void Shell_clearScreen()
{
	/** Put cursor to home */
	Shell_Send( KEY_ESCAPE );
	String_ToShell( "[H" );

	/** Clear entire screen */
	Shell_Send( KEY_ESCAPE );
	String_ToShell( "[2J" );
}

void newPrompt(bool clearScreen) {
	if (clearScreen) Shell_clearScreen();

	Shell_cursorPos = 0;
	memset(command, Null_, sizeof(command));

	String_ToShell(ansi.setFG(ANSI_BRIGHT_YELLOW).c_str());
	String_ToShell(ansi.bold("").c_str());

	String_ToShell("> ");

	String_ToShell(ansi.reset().c_str());

	ShellRunning_Yes;
}

U8   CompStr(const C8* str1, const C8* str2)
{
	const U8 len = strlen(str2);

	if ( strlen(str1) != len ) return NotOK_;

	for (U8 i=0; i<len; i++)
	{
		if ( ToUpper(str1[i]) != ToUpper(str2[i]) )
			return NotOK_;
	}

	return OK_;
}

void execPrompt()
{
	U8 i;

	C8* args = command + CMD_LEN;
	Shell_cursorPos = strlen(command);

	for (i = 0; i < CMD_LEN; i++) {
		if (command[i] == ' ' || command[i] == '\t') {
			command[i] = Null_;
			args = &command[i] + 1;
			break;
		}
	}

	// command = (IsUpperCaseCMD) ? ToUpper(command) : command;

	for (i = 0; i < count_of(commandMap); ++i) {
		if ( CompStr(commandMap[i].command, command) == OK_ ) {
			commandMap[i].function(args);
			return;
		}
	}

	if ( CompStr(command, "clear") == OK_ || CompStr(command, "cls") == OK_ ) {
		Shell_clearScreen();
	} else

	if ( CompStr(command, "exit") == OK_ ) {
		Shell_exit();
	} else

	if ( CompStr(command, "beep") == OK_ ) {
		if (args[0] == 0) args[0] = 1;
		Request_Beep(args[0]);
	} else

	if ( CompStr(command, "hex") == OK_ ) {
		S32 num;

		if ( NumberFromString(args, &num) == NotOK_ ) {
			String_NewLine_ToShell("Invalid number.");
			return;
		}

		HexNumber_ToShell((U32)num);
		NewLine_ToShell(1);
	} else

	if ( CompStr(command, "upper") == OK_ ) {
		UpperCaseCMD_Yes;
	} else
	if ( CompStr(command, "noupper") == OK_ ) {
		UpperCaseCMD_No;
	} else

	if ( CompStr(command, "echo") == OK_ ) {
		String_NewLine_ToShell( args );
	} else

	if ( CompStr(command, "get") == OK_ || CompStr(command, "set") == OK_ ) {
		const bool isGet = ( ToUpper(command[0]) == 'G' );

		const C8* value = args;

		for (i=0; i<strlen(args); i++) {
			if (args[i] == ' ' || args[i] == '\t') {
				args[i] = Null_;
				value = &args[i] + 1;
				break;
			}
		}

		if (args == value && !isGet) {
			String_NewLine_ToShell("No value specified.");
			return;
		}

		const U8 id = getDeviceParameterIdByName(args);

		if (id == 0xff) {
			String_NewLine_ToShell(isGet ? "Parameter not found." : "Bad parameter specified.");
			return;
		}

		if (!isGet && (DeviceParameters[id].type & FLAG_READONLY)) {
			String_ToShell(ansi.setFG(ANSI_BRIGHT_YELLOW).c_str());
			String_NewLine_ToShell("Parameter is read-only.");
			String_ToShell(ansi.reset().c_str());
			return;
		}

		const U8 result = isGet ? DeviceParameters[id].get(args) : DeviceParameters[id].set(value);

		if (result == OK_) {
			if (isGet) {
				String_NewLine_ToShell(args);
			} else {
				String_ToShell(ansi.setFG(ANSI_BRIGHT_GREEN).c_str());
				String_NewLine_ToShell("Success");
				String_ToShell(ansi.reset().c_str());

			}
		} else {
			String_ToShell(ansi.setFG(ANSI_BRIGHT_RED).c_str());
			String_NewLine_ToShell("Failed");
			String_ToShell(ansi.reset().c_str());
		}
	} else

	String_NewLine_ToShell("Command not found.");

}

void String_ToShell(const C8* str)
{
	while(*str > 0) Shell_Send(*str++);
}

void Number_ToShell(S32 N)
{
	U8      i = 0;
	C8      D[10];

	if (N == 0)
	{
		Shell_Send('0');
		return;
	}

	if (N < 0)
	{
		Shell_Send('-');
		N = -N;
	}

	while (N > 0)
	{
		D[i++]= N % 10;
		N /= 10;
	}

	while(i--) Shell_Send(D[i]+'0');
}

void NewLine_ToShell(U8 n)
{
	U8 i;
	if(n == 0) return;
	for(i=0; i<n; i++) String_ToShell("\r\n");
}

void String_NewLine_ToShell(const C8* str)
{
	String_ToShell(str);
	NewLine_ToShell(1);
}

void String_Num_NewLine_ToShell(const C8* str, S32 N)
{
	String_ToShell(str);
	Number_ToShell(N);
	NewLine_ToShell(1);
}

void NumInsertInText_ToShell(const C8* str, S32 num)
{
	/**
	 * Example:  NumInsertInText_ToShell("The parameter is % currently.", 45);
	 *      ==>                          "The parameter is 45 currently."
	 */

	C8      Character;
	Character = *str;
	while (Character)
	{
		if (Character == '%') Number_ToShell(num);
		else                  Shell_Send(Character);
		Character = *str++;
	}
}

void HexNumber_ToShell(U32 num)
{
	U8 i = 0, j;

	C8 hex[] = "0123456789ABCDEF";
	C8 String[10];

	memset(String, Null_, sizeof(String));

	if (num == 0) String[i++] = '0';
	else
	{
		while (num != 0 && i < sizeof(String) - 1)
		{
			U8 digit = num & 0xF;
			String[i++] = hex[digit];
			num >>= 4;
		}
	}

	if (i % 2 != 0 && i<sizeof(String)-1) String[i++] = '0';

	for (j = 0; j < i / 2; j++)
	{
		C8 ch = String[j];
		String[j] = String[i - j - 1];
		String[i - j - 1] = ch;
	}

	for (j = 0; String[j] != Null_; j++)
	{
		Shell_Send(String[j]);
	}
}

bool NumberFromString(const C8* str, S32* num)
{
	S32     N = 0;
	U8      i = 0, s = 0;
	C8      ch;

	if (str == nullptr || num == nullptr) return NotOK_;

	while ((ch = str[i++]) > 0)
	{
		if (ch == ' ' || ch == '\t' || ch == ',')
			continue;

		if (ch == '-')
		{
			if (s > 0) return NotOK_;

			s = 1;
			continue;
		}

		if (ch >= '0' && ch <= '9')
		{
			N = (N * 10) + (ch - '0');
			continue;
		}

		return NotOK_;
	}

	if (s != 0) N = -N;

	*num = N;
	return OK_;
}
