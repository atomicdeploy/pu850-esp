#pragma once

#include "../ASA0002E.h"
#include "../defines.h"

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

U16 HTTP_Port = 80;

const int MAX_WS_CLIENTS = 10;

AsyncWebServer *server;
AsyncWebSocket ws("/ws");

const char index_content[] PROGMEM = R"rawliteral(Pand Caspian PU850)rawliteral" "\n";
