#include "AsyncOTA.h"
#include "AsyncWebServer.h"

AsyncOtaClass AsyncOTA;

void AsyncOtaClass::begin(AsyncWebServer *server) {
	_server = server;

	/*
	// on GET request
	_server->on("/update", HTTP_GET, [&](AsyncWebServerRequest *request){
		AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", UPDATE_HTML, UPDATE_HTML_SIZE);
		response->addHeader("Content-Encoding", "gzip");
		request->send(response);
	});
	*/

	_server->on("/update", HTTP_POST,

	// on POST request
	[&](AsyncWebServerRequest *request) {
		// the request handler is triggered after the upload handler below has finished, send the final response
		AsyncWebServerResponse *response = request->beginResponse(Update.hasError()?500:200, "text/plain", Update.hasError()?Update.getErrorString():"ok!");
		// response->addHeader("Connection", "close");
		// response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	},

	// on file upload
	[&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		// Upload handler chunks in data

		// Start of the request
		if (!index) {
			if (!request->hasHeader("Content-Length")) {
				return request->send(400, "text/plain", "missing `Content-Length` header");
			}

			if (!request->hasParam("MD5", true)) {
				return request->send(400, "text/plain", "missing `MD5` arg");
			}

			if (!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
				return request->send(400, "text/plain", "invalid `MD5` arg");
			}

			const int cmd = (filename == "filesystem") ? U_FS : U_FLASH;
			const size_t fsSize = (size_t)&_FS_end - (size_t)&_FS_start;
			const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
			const size_t updateSize = (request->getHeader("Content-Length")->value().toInt() + 0xFFF) & ~0xFFF;

			if (updateSize > maxSketchSpace) {
				return request->send(400, "text/plain", "too large");
			}

			if (Update.isRunning() && !Update.isFinished()) Update.end();

			Update.runAsync(true); // set the async mode to enabled (yields during writes)

			// Start with max available size
			if (!Update.begin(cmd == U_FS ? fsSize : updateSize, cmd)) {
				// const uint8_t error = Update.getError();
				return request->send(500, "text/plain", "OTA could not begin, error: " + Update.getErrorString());
			}

			os_printf("Update %s\n", filename.c_str());

			/*
			request->onDisconnect([&]() {
				if (Update.isRunning() && !Update.isFinished()) Update.end();
			});
			*/
		}

		// Write chunked data to the free sketch space
		if (len) {
			if (Update.write(data, len) != len) {
				// const uint8_t error = Update.getError();
				return request->send(500, "text/plain", "OTA could not write, error: " + Update.getErrorString());
			}
		}

		// if the final flag is set then this is the last frame of data
		if (final) {
			if (!Update.end(true)) { // true to set the size to the current progress
				// const uint8_t error = Update.getError();
				return request->send(500, "text/plain", "Could not end OTA, error: " + Update.getErrorString());
			}

			// it is required to restart using `ESP.restart()` now
			if (!Update.hasError())	_rebootRequired = true;

			// after finished, the handler in the top will be called
		}

		updateProgress = Update.isRunning() ? (100 * Update.progress()) / Update.size() : 0xff;

		ESP.wdtFeed();
	});

	Update.onStart([&]() {
		ESP.wdtDisable();
		os_printf("Start updating...\n");
		ws.textAll("update:start");
		SendESPStatus_ToPU(Busy_, true);
		updateProgress = 0;
	});

	Update.onProgress([&](size_t progress, size_t total) {
		// os_printf("Progress: %d%%\n", 100 * (progress / total));
		// updateProgress = (100 * progress) / total;
	});

	Update.onError([&](uint8_t error) {
		os_printf("Update error[%u]: %s\n", error, Update.getErrorString().c_str());
		ws.printfAll("update:error,%u", error);
		SendESPStatus_ToPU(Ready_);
		updateProgress = 0xff;
	});

	Update.onEnd([&]() {
		ESP.wdtEnable(0);
		if (_rebootRequired) {
			os_printf("End of update.\n");
			ws.textAll("update:end");
		}
		updateProgress = 0xff;
	});
}
