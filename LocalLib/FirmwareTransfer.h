#pragma once

#include <ESPAsyncWebServer.h>

/**
 * Checks the optional build-time OTA bearer token and sends a 401 response
 * when it is missing or invalid.
 */
bool FirmwareTransferAuthorize(AsyncWebServerRequest* request);

/**
 * Checks the optional OTA bearer token without sending a response.
 */
bool FirmwareTransferHasAuthorization(AsyncWebServerRequest* request);

/**
 * Registers verified sketch-download endpoints on the shared HTTP server.
 */
void RegisterFirmwareTransferRoutes(AsyncWebServer* server);
