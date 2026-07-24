#pragma once

/**
 * @file AsyncWebServer.h
 * @brief HTTP web server and WebSocket configuration
 *
 * Defines the async web server instance, WebSocket endpoint,
 * and related configuration constants.
 */

#include "../ASA0002E.h"
#include "../defines.h"

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

U16 HTTP_Port = 80;

const int MAX_WS_CLIENTS = 10;

AsyncWebServer *server;
AsyncWebSocket ws("/ws");

const char index_content[] PROGMEM = FW_PRODUCT_INDEX_TEXT;
