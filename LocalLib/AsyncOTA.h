#pragma once

/**
 * @file AsyncOTA.h
 * @brief Asynchronous Over-The-Air (OTA) firmware update handler
 *
 * Provides web-based firmware update functionality using the async web server.
 * Supports both firmware and filesystem updates with progress reporting.
 */

#include "Arduino.h"
#include "stdlib_noniso.h"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "flash_hal.h"
#include "FS.h"

class AsyncOtaClass {

public:
	void begin(AsyncWebServer *server);
	void restart();
	bool rebootRequired() { return _rebootRequired; }

private:
	AsyncWebServer *_server;
	bool _rebootRequired = false;
};

extern AsyncOtaClass AsyncOTA;
