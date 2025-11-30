#pragma once
#include "../ASA0002E.h"
#include <ESP8266WiFi.h>

#include <ESPTelnet.h>
#include <EscapeCodes.h>

// const int MAX_TELNET_CLIENTS = 2;

U16 Telnet_Port = 23;

ESPTelnet telnet;
EscapeCodes ansi;

#define IAC  0xFF
#define DONT 0xFE
#define DO   0xFD
#define WONT 0xFC
#define WILL 0xFB

enum {
    STATE_DATA,    // Normal data
    STATE_IAC,     // Saw IAC (0xFF)
    STATE_OPT      // Expecting option after WILL/WONT/DO/DONT
} telnet_state = STATE_DATA;

U8 telnet_cmd;

void TelnetProcessByte(const C8 ch);

// #define STACK_PROTECTOR 512

// WiFiServer Telnet(Telnet_Port);
// WiFiClient telnetClients[MAX_TELNET_CLIENTS];
