#pragma once

// Include ESPAsyncWebServer.h first to get its HTTP_* enum definitions
// This must come before ESPTelnet.h which includes ESP8266WebServer.h
#include <ESPAsyncWebServer.h>

// Guard against ESP8266WebServer.h redefining HTTP_* enums
// This works because ESP8266WebServer.h uses an include guard
#ifndef ESP8266WEBSERVER_H
#define ESP8266WEBSERVER_H
#endif

#include "../ASA0002E.h"
#include <ESP8266WiFi.h>

// Include ESPTelnet without it pulling in ESP8266WebServer's enum definitions
// since we already defined the guard above
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
