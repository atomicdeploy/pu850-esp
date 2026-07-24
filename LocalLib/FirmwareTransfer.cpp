#include "FirmwareTransfer.h"

#include "../ProductConfig.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <libb64/cencode.h>

namespace {

struct FirmwareRange {
	uint32_t start;
	uint32_t end;
	bool partial;
};

bool parseDecimal(const String& value, uint32_t& result) {
	if (value.isEmpty()) return false;

	uint64_t parsed = 0;
	for (size_t index = 0; index < value.length(); ++index) {
		const char character = value[index];
		if (character < '0' || character > '9') return false;
		parsed = parsed * 10 + static_cast<uint8_t>(character - '0');
		if (parsed > UINT32_MAX) return false;
	}

	result = static_cast<uint32_t>(parsed);
	return true;
}

bool parseRange(const String& headerValue, uint32_t totalSize, FirmwareRange& range) {
	if (totalSize == 0) return false;

	String value = headerValue;
	value.trim();
	if (!value.startsWith("bytes=") || value.indexOf(',') >= 0) return false;

	value.remove(0, 6);
	const int separator = value.indexOf('-');
	if (separator < 0 || value.indexOf('-', separator + 1) >= 0) return false;

	const String first = value.substring(0, separator);
	const String last = value.substring(separator + 1);
	if (first.isEmpty() && last.isEmpty()) return false;

	if (first.isEmpty()) {
		uint32_t suffixLength = 0;
		if (!parseDecimal(last, suffixLength) || suffixLength == 0) return false;
		range.start = suffixLength >= totalSize ? 0 : totalSize - suffixLength;
		range.end = totalSize - 1;
		range.partial = true;
		return true;
	}

	if (!parseDecimal(first, range.start) || range.start >= totalSize) return false;

	if (last.isEmpty()) {
		range.end = totalSize - 1;
	} else {
		if (!parseDecimal(last, range.end) || range.start > range.end) return false;
		if (range.end >= totalSize) range.end = totalSize - 1;
	}

	range.partial = true;
	return true;
}

size_t readFlashRange(uint32_t address, uint8_t* output, size_t length) {
	if (output == nullptr || length == 0) return 0;

	const uint32_t flashSize = ESP.getFlashChipSize();
	const uint64_t requestedEnd = static_cast<uint64_t>(address) + length;
	if (requestedEnd > flashSize) return 0;

	const uint32_t alignedAddress = address & ~static_cast<uint32_t>(0x03);
	const size_t prefix = address - alignedAddress;
	const size_t alignedLength = (prefix + length + 3) & ~static_cast<size_t>(0x03);
	if (static_cast<uint64_t>(alignedAddress) + alignedLength > flashSize) return 0;

	if (prefix == 0 &&
		(length & 0x03) == 0 &&
		(reinterpret_cast<uintptr_t>(output) & 0x03) == 0) {
		return ESP.flashRead(alignedAddress, output, length) ? length : 0;
	}

	uint8_t* alignedBuffer = static_cast<uint8_t*>(malloc(alignedLength));
	if (alignedBuffer == nullptr) return 0;

	const bool read = ESP.flashRead(alignedAddress, alignedBuffer, alignedLength);
	if (read) memcpy(output, alignedBuffer + prefix, length);
	free(alignedBuffer);
	return read ? length : 0;
}

int hexNibble(char value) {
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

String sketchMd5Base64(const String& hexadecimal) {
	if (hexadecimal.length() != 32) return String();

	char bytes[16];
	for (size_t index = 0; index < sizeof(bytes); ++index) {
		const int high = hexNibble(hexadecimal[index * 2]);
		const int low = hexNibble(hexadecimal[index * 2 + 1]);
		if (high < 0 || low < 0) return String();
		bytes[index] = static_cast<char>((high << 4) | low);
	}

	char encoded[32] = {};
	base64_encodestate state;
	base64_init_encodestate(&state);
	int length = base64_encode_block(bytes, sizeof(bytes), encoded, &state);
	length += base64_encode_blockend(encoded + length, &state);
	while (length > 0 && (encoded[length - 1] == '\r' || encoded[length - 1] == '\n')) {
		--length;
	}
	encoded[length] = '\0';
	return String(encoded);
}

void sendTransferError(AsyncWebServerRequest* request, int status, const char* message) {
	request->send(
		status,
		"text/plain",
		request->method() == HTTP_HEAD ? "" : message);
}

bool wantsFullFlash(AsyncWebServerRequest* request) {
	if (!request->hasArg("full")) return false;
	String value = request->arg("full");
	value.toLowerCase();
	return value == "1" || value == "true" || value == "yes";
}

bool secureEquals(const String& left, const String& right) {
	const size_t leftLength = left.length();
	const size_t rightLength = right.length();
	const size_t maximumLength = max(leftLength, rightLength);
	size_t difference = leftLength ^ rightLength;

	for (size_t index = 0; index < maximumLength; ++index) {
		const uint8_t leftByte =
			index < leftLength ? static_cast<uint8_t>(left[index]) : 0;
		const uint8_t rightByte =
			index < rightLength ? static_cast<uint8_t>(right[index]) : 0;
		difference |= leftByte ^ rightByte;
	}

	return difference == 0;
}

void addIdentityHeaders(
	AsyncWebServerResponse* response,
	const String& sketchMd5,
	uint32_t totalSize,
	bool fullFlash,
	bool partial) {
	response->addHeader("Accept-Ranges", "bytes");
	response->addHeader("Cache-Control", "no-store");
	response->addHeader("X-Firmware-Mode", fullFlash ? "full" : "sketch");
	response->addHeader("X-Firmware-Size", String(totalSize));
	response->addHeader("X-Product-ID", FW_PRODUCT_ID);
	response->addHeader("X-Product-Model", FW_PRODUCT_MODEL_NAME);
	response->addHeader("X-Firmware-Build", __DATE__ " " __TIME__);

	if (fullFlash) return;

	response->addHeader("X-Firmware-MD5", sketchMd5);
	response->addHeader("ETag", String("\"") + sketchMd5 + "\"");

	if (!partial) {
		const String encodedMd5 = sketchMd5Base64(sketchMd5);
		if (!encodedMd5.isEmpty()) {
			response->addHeader("Content-MD5", encodedMd5);
			response->addHeader("Digest", "md5=" + encodedMd5);
		}
	}
}

} // namespace

bool FirmwareTransferHasAuthorization(AsyncWebServerRequest* request) {
	const char* configuredToken = FW_OTA_BEARER_TOKEN;
	if (configuredToken[0] == '\0') return true;

	const AsyncWebHeader* header = request->getHeader("Authorization");
	const String expected = String("Bearer ") + configuredToken;
	return header != nullptr && secureEquals(header->value(), expected);
}

bool FirmwareTransferAuthorize(AsyncWebServerRequest* request) {
	if (FirmwareTransferHasAuthorization(request)) return true;

	AsyncWebServerResponse* response = request->beginResponse(
		401,
		"text/plain",
		request->method() == HTTP_HEAD ? "" : "Unauthorized");
	response->addHeader(
		"WWW-Authenticate",
		"Bearer realm=\"" FW_PRODUCT_ID " firmware\"");
	response->addHeader("Cache-Control", "no-store");
	request->send(response);
	return false;
}

void RegisterFirmwareTransferRoutes(AsyncWebServer* server) {
#if FW_ENABLE_FIRMWARE_DOWNLOAD
	server->on(
		"/firmware/download",
		HTTP_HEAD | HTTP_GET,
		[](AsyncWebServerRequest* request) {
			if (!FirmwareTransferAuthorize(request)) return;

			const bool fullFlash = wantsFullFlash(request);
#if !FW_ENABLE_FULL_FLASH_DOWNLOAD
			if (fullFlash) {
				sendTransferError(request, 403, "Full-flash download is disabled by this build");
				return;
			}
#endif

			const uint32_t totalSize =
				fullFlash ? ESP.getFlashChipSize() : ESP.getSketchSize();
			if (totalSize == 0) {
				sendTransferError(request, 500, "Firmware size is unavailable");
				return;
			}

			FirmwareRange range = {0, totalSize - 1, false};
			if (request->hasHeader("Range")) {
				const AsyncWebHeader* header = request->getHeader("Range");
				if (header == nullptr || !parseRange(header->value(), totalSize, range)) {
					AsyncWebServerResponse* response = request->beginResponse(
						416,
						"text/plain",
						request->method() == HTTP_HEAD ? "" : "Invalid byte range");
					response->addHeader("Content-Range", "bytes */" + String(totalSize));
					response->addHeader("Cache-Control", "no-store");
					request->send(response);
					return;
				}
			}

			const uint32_t contentLength = range.end - range.start + 1;
			const String sketchMd5 = ESP.getSketchMD5();
			const String etag = String("\"") + sketchMd5 + "\"";
			if (!fullFlash &&
				!range.partial &&
				request->hasHeader("If-None-Match") &&
				request->getHeader("If-None-Match")->value() == etag) {
				AsyncWebServerResponse* response = request->beginResponse(304);
				addIdentityHeaders(response, sketchMd5, totalSize, false, false);
				request->send(response);
				return;
			}

			AsyncWebServerResponse* response = nullptr;
			if (request->method() == HTTP_HEAD) {
				response = request->beginResponse(
					range.partial ? 206 : 200,
					"application/octet-stream",
					"");
				response->setContentLength(contentLength);
			} else {
				response = request->beginResponse(
					"application/octet-stream",
					contentLength,
					[start = range.start, contentLength](
						uint8_t* output,
						size_t maximumLength,
						size_t offset) -> size_t {
						if (offset >= contentLength) return 0;
						const size_t remaining = contentLength - offset;
						const size_t requested = min(remaining, maximumLength);
						return readFlashRange(start + offset, output, requested);
					});
				if (range.partial) response->setCode(206);
			}

			const String filename =
				fullFlash
					? String(FW_PRODUCT_ID) + "-flash.bin"
					: String(FW_PRODUCT_ID) + "-firmware.bin";
			response->addHeader(
				"Content-Disposition",
				"attachment; filename=\"" + filename + "\"");
			if (range.partial) {
				response->addHeader(
					"Content-Range",
					"bytes " + String(range.start) + "-" + String(range.end) +
						"/" + String(totalSize));
			}
			addIdentityHeaders(
				response,
				sketchMd5,
				totalSize,
				fullFlash,
				range.partial);
			request->send(response);
		});
#else
	(void)server;
#endif
}
