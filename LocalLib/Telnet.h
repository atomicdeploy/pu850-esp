#pragma once

/*
 * ESPTelnet includes ESP8266WebServer.h. Pull the async server's HTTP method
 * definitions in first, then suppress the conflicting synchronous header.
 */
#include <ESPAsyncWebServer.h>
#ifndef ESP8266WEBSERVER_H
#define ESP8266WEBSERVER_H
#endif

#include "../ASA0002E.h"
#include <ESP8266WiFi.h>
#include <ESPTelnet.h>

#define TELNET_SE   0xF0
#define TELNET_SB   0xFA
#define TELNET_WILL 0xFB
#define TELNET_WONT 0xFC
#define TELNET_DO   0xFD
#define TELNET_DONT 0xFE
#define TELNET_IAC  0xFF

#define TELNET_OPTION_ECHO 0x01
#define TELNET_OPTION_SGA  0x03

enum TelnetInputState : U8 {
	TelnetState_Data = 0,
	TelnetState_IAC,
	TelnetState_Option,
	TelnetState_Subnegotiation,
	TelnetState_SubnegotiationIAC,
};

extern bool Telnet_Initialized;
extern U16 Telnet_Port;
extern ESPTelnet telnet;

void TelnetProcessByte(U8 ch);
void TelnetResetParser();
bool Telnet_Setup();
void Telnet_End();
void Telnet_Service();
