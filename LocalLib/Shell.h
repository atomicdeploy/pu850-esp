#pragma once
#include "../ASA0002E.h"
#include "Commands.h"

U8 Shell_Flags=0;

U8 Shell_cursorPos = 0;
#define ShellRunning_Yes        Shell_Flags High_0
#define ShellRunning_No         Shell_Flags Low_0
#define IsShellRunning          ((Shell_Flags IsHigh_0

#define EscFlag_Yes             Shell_Flags High_1
#define EscFlag_No              Shell_Flags Low_1
#define IsEscFlag               ((Shell_Flags IsHigh_1

#define ExtendedMode_Yes        Shell_Flags High_2
#define ExtendedMode_No         Shell_Flags Low_2
#define IsExtendedMode          ((Shell_Flags IsHigh_2

#define UpperCaseCMD_Yes        Shell_Flags High_3
#define UpperCaseCMD_No         Shell_Flags Low_3
#define IsUpperCaseCMD          ((Shell_Flags IsHigh_3

#define CMD_LEN 64
C8 command[CMD_LEN + 1];
C8 escMode;
// U8 ttyRows = 0, ttyCols = 0;

// --------------------------------------------------

C8 lastCharEsc = 0;

#define KEY_BELL   0x07
#define KEY_ESCAPE 0x1B // '\e'

#define CTRL_C     0x03
#define CTRL_L     0x0C
#define KEY_BKSP   0x08
#define KEY_TAB    0x09 // '\t'
#define KEY_RETURN 0x0A // '\r' <CR>
#define KEY_ENTER  0x0D // '\n' <LF>
#define KEY_ERASE  0x7F // Backspace

#define ARR_UP     0x41 // 65 'A'
#define ARR_DOWN   0x42 // 66 'B'
#define ARR_RIGHT  0x43 // 67 'C'
#define ARR_LEFT   0x44 // 68 'D'
#define ARR_END    0x46 // 70 'F'
#define ARR_HOME   0x48 // 72 'H'

// --------------------------------------------------

void sendCursor(U8 direction);
void sendCursorMulti(U8 direction, U8 num);
void shiftToLeft();
void shiftToRight();
void fixPos(bool clearAfter);
void newPrompt(bool clearScreen);
void execPrompt();
void Shell_clearScreen();
void ShellService(C8 ch);

U8   CompStr(const C8* str1, const C8* str2);
void String_ToShell(const C8* str);
void Number_ToShell(S32 N);
void NewLine_ToShell(U8 n);
void String_NewLine_ToShell(const C8* str);
void String_Num_NewLine_ToShell(const C8* str, S32 N);
void NumInsertInText_ToShell(const C8* str, S32 num);
void HexNumber_ToShell(U32 num);
bool NumberFromString(const C8* str, S32* num);

inline C8 ToUpper(C8 ch) { return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch; }

#ifdef ShellOnSerial
#define Shell_Send  Serial.write
#define Shell_exit  exitshell
void exitshell() {}
#else
#define Shell_Send  putchar1
#define Shell_exit  exitshell
void putchar1(C8 c);
void exitshell();
#endif
