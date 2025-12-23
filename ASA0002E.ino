/************************************************************
 * Project code            : ASA0002E
 * Project name            : PU850's ESP8266 Firmware
 * Version                 : 2.1.0
 * Compiler                : gcc (built-in Arduino platform)
 * Editor                  : VS Code
 * Date                    : 1403/06/15
 * Ordered from            : Faravary Pand Caspian
 * Author                  : David@Refoua.me
 * Module Type             : Generic ESP8266 Module
 * Chip type               : ESP8266
 * CPU Clock frequency     : 80.000000 MHz
 * Flash Size              : 1.00 MB
 * Upload Speed            : 115200
 * Flash Mode              : QIO (fast)
 * Flash Frequency         : 40 MHz
************************************************************/

#include "ASA0002E.h"
#include "defines.h"

#include "Arduino.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
#include <list>

#include "Uri.h"
#include "Hash.h"
#include "md5.h"

#include <libb64/cencode.h>
#include <libb64/cdecode.h>

#include "LwipIntf.cpp"
#include "lwip/opt.h"
#include "lwip/udp.h"
#include "lwip/inet.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/mem.h"
#include <LwipDhcpServer.h>

#include <ESP8266WiFi.h>
#include <esp8266_peri.h>
// #include <ESP8266WebServer.h>
// #include <ESP8266HTTPClient.h>
// #include <ESP8266WiFiMulti.h>
// #include <ESP8266Ping.h>
#include <ESP8266mDNS.h>
#include <ESP8266NetBIOS.h>
#include <ESP8266LLMNR.h>
// #include <ESP8266SSDP.h>
// #include <WebSocketsServer.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

// #include <NetDiscovery.h>

#include <NTPClient.h>

// #include <DNSServer.h>
// #include <StreamString.h>

// #include <LittleFS.h>
// #include <FS.h>

#include <ESP_EEPROM.h>

// #include <AsyncMessagePack.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <JSON.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
// #include <AsyncElegantOTA.h>

#include "LocalLib/AsyncWebServer.h"
#include "LocalLib/AsyncOTA.h"
#include "LocalLib/Sessions.h"
#include "LocalLib/Authentication.h"
#include "LocalLib/SSDP.h"
#include "LocalLib/MNDP.h"

#include "LocalLib/Shell.h"
#include "LocalLib/Shell.cpp"
#include "LocalLib/Telnet.h"
#include "LocalLib/Telnet.cpp"
#include "LocalLib/Public.h"
#include "LocalLib/UART.h"

// #include <ArduinoOTA.h>

#include "LocalLib/Public.cpp"
#include "LocalLib/UART.cpp"

#include "LocalLib/AsyncWebServer.cpp"
#include "LocalLib/Sessions.cpp"
#include "LocalLib/SSDP.cpp"
#include "LocalLib/MNDP.cpp"

// #include "LocalLib/OTA.cpp"
#include "LocalLib/AsyncOTA.cpp"

// #include "LocalLib/RemoteCommand.cpp"

#include "LocalLib/CrashHandler.h"
#include "LocalLib/CrashHandler.cpp"

// DNSServer dnsServer;

WiFiUDP ntpUDP;

// NTP client configured to use pool.ntp.org with UTC timezone
// Update interval: 60000ms (1 minute)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// JSONVar dataValues;

// ---------------------------------------------------------------

void WiFi_Setup()
{
	// WIFI_OFF    = 0
	// WIFI_STA    = 1
	// WIFI_AP     = 2
	// WIFI_AP_STA = 3

	WiFi.persistent(false); // Don't save configuration in flash, we use our own

	WiFi.mode(WIFI_OFF);
	WiFi.disconnect(true);
	WiFi.config(0U, 0U, 0U); // enable DHCP (INADDR_NONE)

	onValueUpdate(ESP_Suffix_HostName, 0); // Perform `WiFi.setHostname(flashSettings.hostname)`

	WiFi.softAPdisconnect(true); // clear AP's SSID and pre-shared key and disable AP mode.

	setNetFlags(flashSettings.net_flags);

	const U8 wifi_mode = wifi_get_opmode();

	const bool sta_enabled = (wifi_mode & WIFI_STA),
		   ap_enabled  = (wifi_mode & WIFI_AP);

	if (ap_enabled)
	{
		// Configure static IP for AP mode (192.168.4.1)
		WiFi.softAPConfig( IPAddress(192,168,4,1), IPAddress(0,0,0,0), IPAddress(255,255,255,0) );
	}

	if (sta_enabled)
	{
		// Configure static IP for station mode if DHCP is disabled
		if ( !flashSettings.dhcp_enabled )
		{
			IPAddress ip(flashSettings.static_ip[0], flashSettings.static_ip[1], 
			             flashSettings.static_ip[2], flashSettings.static_ip[3]);
			IPAddress gateway(flashSettings.gateway[0], flashSettings.gateway[1], 
			                  flashSettings.gateway[2], flashSettings.gateway[3]);
			IPAddress subnet(flashSettings.subnet[0], flashSettings.subnet[1], 
			                 flashSettings.subnet[2], flashSettings.subnet[3]);
			IPAddress dns(flashSettings.dns[0], flashSettings.dns[1], 
			              flashSettings.dns[2], flashSettings.dns[3]);
			WiFi.config(ip, gateway, subnet, dns);
		}
	}

	if (wifi_mode != WIFI_OFF)
	{
		WiFi.forceSleepWake(); // wake up WiFi modem from sleep, if required
	}

	if (ap_enabled)
	{
		WiFi.softAP(flashSettings.ap_ssid, flashSettings.ap_password);  // SoftAP Mode
	}
	else
	{
		WiFi.softAPdisconnect(true);
	}

	if (sta_enabled)
	{
		WiFi.begin(flashSettings.sta_ssid, flashSettings.sta_password); // Station Mode
	}
	else
	{
		WiFi.disconnect(true);
	}

	WiFi.setAutoReconnect(sta_enabled ? true : false);
	WiFi.setAutoConnect(sta_enabled ? true : false);

	if (wifi_mode == WIFI_OFF)
	{
		WiFi.forceSleepBegin(); // power off the WiFi hardware
	}

	// Call `SendNetStatus_ToPU()` in `loop()` later
	oldNetStatus = 0xff;
}

void Settings_Clear()
{
	flashSettings.crc32 = (settingsError != 2) ? ErrorNum_ : flashSettings.crc32;

	flashSettings.net_flags = 0;

	memset(flashSettings.hostname,     Null_, sizeof(flashSettings.hostname));
	memset(flashSettings.ap_ssid,      Null_, sizeof(flashSettings.ap_ssid));
	memset(flashSettings.ap_password,  Null_, sizeof(flashSettings.ap_password));
	memset(flashSettings.sta_ssid,     Null_, sizeof(flashSettings.sta_ssid));
	memset(flashSettings.sta_password, Null_, sizeof(flashSettings.sta_password));
	
	// Initialize static IP settings with defaults
	flashSettings.dhcp_enabled = true; // Enable DHCP by default
	
	// Default static IP: 192.168.1.100
	flashSettings.static_ip[0] = 192;
	flashSettings.static_ip[1] = 168;
	flashSettings.static_ip[2] = 1;
	flashSettings.static_ip[3] = 100;
	
	// Default gateway: 192.168.1.1
	flashSettings.gateway[0] = 192;
	flashSettings.gateway[1] = 168;
	flashSettings.gateway[2] = 1;
	flashSettings.gateway[3] = 1;
	
	// Default subnet mask: 255.255.255.0
	flashSettings.subnet[0] = 255;
	flashSettings.subnet[1] = 255;
	flashSettings.subnet[2] = 255;
	flashSettings.subnet[3] = 0;
	
	// Default DNS: 8.8.8.8 (Google DNS)
	flashSettings.dns[0] = 8;
	flashSettings.dns[1] = 8;
	flashSettings.dns[2] = 8;
	flashSettings.dns[3] = 8;
	
	// Default port settings
	flashSettings.web_port = 80;
	flashSettings.telnet_port = 23;
}

bool Settings_Read()
{
	U16 addr = 0;

	settingsError = 0xff;

	Settings_Clear();

	if (EEPROM.percentUsed() >= 0)
	{
		EEPROM.get(addr, flashSettings); addr += sizeof(flashSettings);

		const U32 crcOfData = calculateCRC32(((U8*) &flashSettings) + sizeof(flashSettings.crc32), sizeof(flashSettings) - sizeof(flashSettings.crc32));

		if (crcOfData == flashSettings.crc32)
		{
			settingsError = 0;
			return OK_;
		}
	}

	settingsError = (flashSettings.crc32 == ErrorNum_) ? SETTINGS_ERROR_NO_DATA : SETTINGS_ERROR_CORRUPTED;

	Settings_Clear();

	flashSettings.net_flags = getNetFlags();

	memcpy(flashSettings.hostname, wifi_station_hostname, sizeof(flashSettings.hostname) - 1);

	struct station_config config;

	if (wifi_station_get_config(&config))
	{
		strncpy(flashSettings.sta_ssid,     (C8*) config.ssid,     sizeof(flashSettings.sta_ssid)     - 1);
		strncpy(flashSettings.sta_password, (C8*) config.password, sizeof(flashSettings.sta_password) - 1);
	}

	struct softap_config ap_config;

	if (wifi_softap_get_config(&ap_config))
	{
		strncpy(flashSettings.ap_ssid,     (C8*) ap_config.ssid,     sizeof(flashSettings.ap_ssid)     - 1);
		strncpy(flashSettings.ap_password, (C8*) ap_config.password, sizeof(flashSettings.ap_password) - 1);
	}

	return NotOK_;
}

bool Settings_Write()
{
	U16 addr = 0;

	flashSettings.crc32 = calculateCRC32(((U8*) &flashSettings) + sizeof(flashSettings.crc32), sizeof(flashSettings) - sizeof(flashSettings.crc32));

	EEPROM.put(addr, flashSettings); addr += sizeof(flashSettings);

	const bool success = EEPROM.commit() ? OK_ : NotOK_;

	if (success == OK_)
	{
		settingsError = 0;
	}

	return success;
}

void StreamBulkDataAsResponse(AsyncWebServerRequest *request)
{
	dataTimeOut = millis();
	eMasterStatus = Busy_;
	onValueUpdate(ESP_Suffix_Status, eMasterStatus);

	AsyncClient* client = request->client();

	AsyncWebServerResponse *response = request->beginChunkedResponse("application/octet-stream", [&](uint8_t *writeBuffer, size_t maxWriteLength, size_t totalWritten) -> size_t {
		U32 writtenLength = 0;

		if (!BulkDetected)
		{
			LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

			Cancel_FileReceiveOperation(true);
			return writtenLength;
		}

		while (!writtenLength)
		{
			SerialPortReceiveProcess();

			while (GetBulkRemain() > 0 && writtenLength < maxWriteLength)
				writeBuffer[writtenLength++] = ReadFromBulk();

			if (writtenLength > 0) {
				dataTimeOut = millis();
			}

			if (FinalDataSize && BulkTotalRead >= FinalDataSize) {
				eOldMasterStatus = 0xff;
				dataTimeOut = 0;
				BulkReset(0);
				return writtenLength;
			}

			if (BulkOverflow || eProcessState != WaitforBulk_) {
				LogMessageInFile("Cancelling File Receive Operation (terminated = false)");

				Cancel_FileReceiveOperation(false);
				return writtenLength;
			}
		}

		return writtenLength;
	});

	client->onPoll([&](void *arg, AsyncClient *client) {
		if (BulkDetected) return;

		LogMessageInFile("Cancelling File Receive Operation (terminated = false)");

		Cancel_FileReceiveOperation(false);
		if (client->connected()) client->stop();
	}, NULL);

	request->onDisconnect([&]() {
		LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

		Cancel_FileReceiveOperation(true);
		if (BulkOverflow) ws.textAll("msg:5,overflow"); // inform of overflow if `BulkOverflow` was set
	});

	request->send(response);
}

// ---------------------------------------------------------------

// Add Cross-Origin headers to the response
AsyncCorsMiddleware cors;

// Called when `/login` route is reached
LoginHandler loginHandler;

// Collects the extracted username/password and session token credentials
AuthenticationMiddleware authMiddleware;

// Only allows requests with valid session ids to pass
AuthorizationMiddleware authzMiddleware;

// Store the username/password and token in the request attributes
void AuthenticationMiddleware::collectCreds(AsyncWebServerRequest *request) {
	String username = "", password = "", token = "";

	// Extract credentials from the Authorization request header
	const AsyncWebHeader* authHeader = request->getHeader(F("Authorization"));

	if (authHeader != nullptr) {
		const String value = authHeader->value();

		// Extract the username and password
		if (value.startsWith(F("Basic "))) {
			const String encodedCredentials = value.substring(6); // Skip "Basic " prefix (6 characters)

			// Decode the base64-encoded credentials
			base64_decodestate state;
			base64_init_decodestate(&state);
			char decodedCredentials[encodedCredentials.length()]; // Adjust the buffer size as needed
			const int decodedLength = base64_decode_block(encodedCredentials.c_str(), encodedCredentials.length(), decodedCredentials, &state);
			decodedCredentials[decodedLength] = '\0';

			// Find the colon separator index
			int colonIndex = -1;
			for (int i = 0; i < decodedLength; i++) {
				if (decodedCredentials[i] == ':') {
					colonIndex = i;
					break;
				}
			}

			if (colonIndex != -1) {
				username = String(decodedCredentials).substring(0, colonIndex);
				password = String(decodedCredentials).substring(colonIndex + 1);
			}
		}

		// Extract the session token
		else if (value.startsWith(F("Bearer ")) ){
			if (value.length() <= 7 + TOKEN_LENGTH)
			{
				token = value.substring(7, 7 + TOKEN_LENGTH); // Skip "Bearer " prefix (7 characters)
			}
		}
	}

	// Extract the username and password from the request parameters (only for `/login` route)
	if (LoginHandler::isLoginRequest(request)) {
		if (request->hasArg(F("username")))
			username = request->arg(F("username"));

		if (request->hasArg(F("password")))
			password = request->arg(F("password"));

//List all parameters
int params = request->params();
for(int i=0;i<params;i++){
  const AsyncWebParameter* p = request->getParam(i);
  if(p->isFile()){ //p->isPost() is also true
    Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
  } else if(p->isPost()){
    Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
  } else {
    Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
  }
}

//List all parameters (Compatibility)
int args = request->args();
for(int i=0;i<args;i++){
  Serial.printf("ARG[%s]: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
}

	}

	// Extract the authentication token from the request cookies
	const AsyncWebHeader* cookiesHeader = request->getHeader(F("Cookie"));

	if (cookiesHeader != nullptr) {
		const String value = cookiesHeader->value();

		const int tokenIndex = value.indexOf(F("TOKEN="));

		if (tokenIndex != -1)
		{
			int endIndex = value.indexOf(';', tokenIndex);
			if (endIndex == -1) endIndex = value.length();

			int tokenLength = endIndex - (tokenIndex + 6); // 6 is the length of "TOKEN="

			if (tokenLength > 0 && tokenLength <= TOKEN_LENGTH) {
				token = value.substring(tokenIndex + 6, endIndex);
			}
		}
	}

	// Extract the token from the request query parameters
	if (request->hasArg(F("token"))) {
		token = request->arg(F("token"));
	}

	// Store the extracted credentials in the request attributes
	storeAttribute(request, "username", username);
	storeAttribute(request, "password", password);
	storeAttribute(request, "token", token);
}

// Check if the session token is valid
int AuthenticationMiddleware::checkSession(AsyncWebServerRequest *request) const {
	int sessionId = -1;

	char token_bytes[TOKEN_LENGTH / 2];

	// Extract the session token from the request attributes
	const String token = String(request->getAttribute("token", ""));

	// Check if the session token is valid
	if (token.length() == TOKEN_LENGTH && parseHexString(token, token_bytes)) {

		// Check if the session token is present in the sessions list
		sessionId = validateSession(token_bytes, request->client()->remoteIP());

		storeAttribute(request, "session_id", String(sessionId));
	}

	return sessionId;

}

// Called when the `/login` route is reached
void LoginHandler::handleLogin(AsyncWebServerRequest *request) {
	// Extract the username and password from the request attributes
	const String username = String(request->getAttribute("username", ""));
	const String password = String(request->getAttribute("password", ""));


}

// ---------------------------------------------------------------

void setup()
{

	Serial.begin(BAUD_RATE);
	Serial.setRxBufferSize(UART_BUFFER_SIZE);

	#ifdef DebugOnSerial
	Serial.setDebugOutput(true);
	// USC0(0) |= (1 << UCLBE); // enable SERIAL_LOOPBACK
	#endif

	pinMode(GWP, OUTPUT); digitalWrite(GWP, LOW); // Set as ready on boot

	#ifdef DebugTools
	// WriteToDebug(ESP.getResetInfo());
	#endif

	// ---------------------------------------------------------------

	firmwareVersion = ESP.getSketchMD5();

	firmwareIsValid = ESP.checkFlashCRC();

	EEPROM.begin(max((size_t)sizeof(flashSettings), EEPROM_MIN_SIZE));

	Settings_Read();

	// ---------------------------------------------------------------

	// stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&onStationConnected);
	// stationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected(&onStationDisconnected);

	// WiFiEventHandler stationConnectedHandler;
	// WiFiEventHandler stationDisconnectedHandler;
	// WiFiEventHandler probeRequestPrintHandler;
	// WiFiEventHandler probeRequestBlinkHandler;

	// void onStationConnected(const WiFiEventSoftAPModeStationConnected& event) { }
	// void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected& event) { }

	wifi_set_event_handler_cb(wifi_handle_event_cb);

	WiFi_Setup();

	// ---------------------------------------------------------------

	// dnsServer.start(53, "*", WiFi.softAPIP());

	delete server;

	server = new AsyncWebServer(HTTP_Port);

	// ArduinoOTA.setHostname(WiFi.hostname().c_str());
	// ArduinoOTA.begin();

	// AsyncElegantOTA.setID(WiFi.hostname().c_str());

	// AsyncElegantOTA.begin(server);

	AsyncOTA.begin(server);

	MDNS.begin(WiFi.hostname().c_str());
	NBNS.begin(WiFi.hostname().c_str());
	LLMNR.begin(WiFi.hostname().c_str());

	MDNS.addService("http", "tcp", HTTP_Port); // service name, protocol, port

	initSSDP();

	initWebSocket();

	initSessions();

	timeClient.begin();

	server->onNotFound([&](AsyncWebServerRequest *request) {
		/*
		if (request->method() == HTTP_OPTIONS) {
			return request->send(200);
		}
		*/

		request->send(404, "text/plain", "API Not Found");
	});

	server->on("/beep", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		const int count = request->hasArg(F("count")) ? request->arg(F("count")).toInt() : 1;
		Request_Beep(count);
		request->send(200, "text/plain", "beep");
	});

	server->on("/message/send", HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("text")))
		{
			request->send(400, "text/plain", "missing `text` argument");
			return;
		}

		const String text = request->arg(F("text"));

		const bool success = Request_SendMessage_ToPU(text.c_str(), 0, 0, ID_WiFiIcon_);

		request->send(200, "text/plain", success == OK_ ? "ok!" : "fail");
	});

	server->on("/weight/display", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		bool success = false;

		if (!request->hasArg(F("value")) && request->method() != HTTP_POST)
		{
			const U8 result = Request_IsWeightHidden();

			if (result >= UnDefinedNum_)
			{
				request->send(400, "text/plain", "fail");
				return;
			}

			request->send(200, "text/plain", result == Yes_ ? "hidden" : (result == No_ ? "shown" : "unknown"));
			return;
		}

		const String arg = request->arg(F("value"));

		if (arg == "show")
		{
			success = Request_ShowHideWeight(No_);
		}
		else if (arg == "hide")
		{
			success = Request_ShowHideWeight(Yes_);
		}
		else
		{
			request->send(400, "text/plain", "invalid");
			return;
		}

		request->send(200, "text/plain", success ? "ok!" : "fail");
	});

	server->on("/action/tare", HTTP_POST, [&](AsyncWebServerRequest *request) {
		const bool success = Request_Tare(0);
		request->send(200, "text/plain", success == OK_ ? "ok!" : "fail");
	});

	server->on("/power", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const bool success = Request_GivePower();
		request->send(200, "text/plain", success == OK_ ? String(ePowerStatus) + "," + String(eBatteryValue) : "fail");
	});

	server->on("/weight/get", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const S32 result = Request_GiveWeight();
		request->send(200, "text/plain", result != UnDefinedNum_ ? String(result) : "fail");
	});

	server->on("/receipt", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		const bool isTimeout = ((U32)millis() - dataTimeOut) >= 2*validTimeOut_;

		if ((BulkDetected && !isTimeout) || (eMasterStatus == Busy_ || eProcessState == WaitforBulk_))
		{
			request->send(503, "text/plain", request->method() != HTTP_HEAD ? "device is busy" : "");
			return;
		}

		if (!request->hasArg(F("id")))
		{
			request->send(400, "text/plain", "invalid");
			return;
		}

		const U32 id = request->arg(F("id")).toInt();

		const U8 result = Request_GiveReceipt(id);

		if (result == result_Success || result == result_Processing)
		{
			return StreamBulkDataAsResponse(request);
		}

		switch (result)
		{
			case result_Fail:		request->send(500, "text/plain", "fail");		return;
			case result_Not_Found:		request->send(404, "text/plain", "not found");		return;
			case result_Unauthorised: 	request->send(403, "text/plain", "forbidden");		return;
			case result_NotAcceptable:	request->send(403, "text/plain", "not acceptable");	return;
		}

		if (result == 0xff) {
			request->send(500, "text/plain", "device unavailable");
			return;
		}

		request->send(500, "text/plain", "result: " + String(result));
	});

	/*
	server->on("/report", HTTP_GET, [&](AsyncWebServerRequest *request) {
		// if ( request->hasArg(F("from"))) && request->hasArg(F("to"))) )

		if ( request->hasArg(F("parm")) )
		{
			U32 parm = request->arg(F("parm")).toInt();

			U32 result = Request_GiveReport(parm);

			if ( result == result_Success && strlen(BulkBuffer) > 0 )
			{
				request->send(200, "text/plain", BulkBuffer );
			}
			else
			{
				request->send(200, "text/plain", "result: " + String(result));
			}

			return;
		}
		else if ( request->hasArg(F("fromYear"))) && request->hasArg(F("fromMonth"))) && request->hasArg(F("fromDay"))) && request->hasArg(F("toYear"))) && request->hasArg(F("toMonth"))) && request->hasArg(F("toDay")))  )
		{
			U16 fromYear = request->arg(F("fromYear"))).toInt();
			U8 fromMonth = request->arg(F("fromMonth"))).toInt();
			U8 fromDay = request->arg(F("fromDay"))).toInt();
			U16 toYear = request->arg(F("toYear"))).toInt();
			U8 toMonth = request->arg(F("toMonth"))).toInt();
			U8 toDay = request->arg(F("toDay"))).toInt();

			if ( isValidDate(fromYear, fromMonth, fromDay) && isValidDate(toYear, toMonth, toDay) )
			{

				if (fromYear > 1000) fromYear = ShamsiU16toU8(fromYear);
				if (toYear   > 1000) toYear   = ShamsiU16toU8(toYear);

				U32 result = Request_GiveReport(fromYear, fromMonth, fromDay,  toYear, toMonth, toDay);

				if ( result == result_Success && strlen(BulkBuffer) > 0 )
				{
					request->send(200, "text/plain", BulkBuffer );
				}
				else
				{
					request->send(200, "text/plain", "result: " + String(result));
				}

				return;
			}
		}

		request->send(400, "text/plain", "invalid");
	});
	*/

	server->on("/file/cancel", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

		Cancel_FileReceiveOperation(true);
		request->send(200, "text/plain", "canceled");
	});

	server->on("/file/download", HTTP_HEAD|HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("filename")))
		{
			request->send(400, "text/plain", request->method() != HTTP_HEAD ? "missing `filename` argument" : "");
			return;
		}

		const bool isTimeout = ((U32)millis() - dataTimeOut) >= 2*validTimeOut_;

		if (!(BulkDetected && !isTimeout) && (eMasterStatus == Busy_ || eProcessState == WaitforFile_))
		{
			LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

			Cancel_FileReceiveOperation(true);
		}

		if (eMasterStatus == Busy_)
			WaitForPU_Ready();

		if ((BulkDetected && !isTimeout) || (eMasterStatus == Busy_ || eProcessState == WaitforFile_))
		{
			request->send(503, "text/plain", request->method() != HTTP_HEAD ? "device is busy" : "");
			return;
		}

		AsyncClient* client = request->client();

		const String filename = request->arg(F("filename"));

		ResourceDateTime = ErrorNum_;

		memset(E_FileName, Null_, sizeof(E_FileName));

		if (request->method() != HTTP_HEAD) Request_GiveFileMetadata(filename.c_str());

		const String basename = String(strlen(E_FileName) != 0 ? E_FileName : "\0");

		U32 total = 0xffffffff, start = 0, end = 0;

		bool is_range = false;

		if (request->method() != HTTP_HEAD && request->hasHeader(F("Range")))
		{
			String range = request->getHeader("Range")->value();

			range.toLowerCase();

			if (!range.startsWith("bytes="))
			{
				request->send(416, "text/plain", request->method() != HTTP_HEAD ? "unsupported range" : "");
				return;
			}

			bool from_end = false;

			range.remove(0, 6);

			if (range.indexOf('-') != -1 && range.length() > 1)
			{
				const String startStr = range.substring(0, range.indexOf('-')),
					       endStr = range.substring(   range.indexOf('-') + 1);

				start = startStr.length() > 0 ? startStr.toInt() : 0;
				end   =   endStr.length() > 0 ?   endStr.toInt() : 0xffffffff;

				if (startStr.length() == 0 && endStr.length() > 0)
					from_end = true;
			}

			if (start > end || range.indexOf('-') == -1 || range.length() < 2)
			{
				request->send(416, "text/plain", request->method() != HTTP_HEAD ? "invalid range specified" : "");
				return;
			}

			if (Request_GiveFileMetadata(filename.c_str()) == result_Success)
			{
				total = FinalDataSize;
				end = min(total - 1, end);

				if (from_end)
				{
					start = total - end;
					end   = total - 1;

					if (total == 1 && end == 0) start = 0;
				}

				is_range = true;
			}
			else
			{
				start = 0;
				end   = 0;

				is_range = false;
			}

			if (start >= total)
			{
				request->send(416, "text/plain", request->method() != HTTP_HEAD ? "invalid range specified" : "");
				return;
			}
		}

		const U8 result = request->method() == HTTP_HEAD ? Request_GiveFileMetadata(filename.c_str()) : Request_GiveFile(filename.c_str(), start, is_range ? 1 + end - start : 0);

		switch (result)
		{
			case result_Success:
				if (strlen(E_FileName) == 0 || filename.length() == 0)
				{
					request->send(500, "text/plain", request->method() != HTTP_HEAD ? "file unavailable" : "");
					return;
				}
				break;

			case result_Not_Found:
				request->send(404, "text/plain", request->method() != HTTP_HEAD ? "file not found" : "");
				return;

			case result_NotAcceptable:
				request->send(403, "text/plain", request->method() != HTTP_HEAD ? "not acceptable" : "");
				return;

			case result_Fail:
				request->send(500, "text/plain", request->method() != HTTP_HEAD ? "unable to start file transfer" : "");
				return;

			case result_Undefined:
				request->send(504, "text/plain", request->method() != HTTP_HEAD ? "no response from server" : "");
				return;

			default:
				request->send(400, "text/plain", request->method() != HTTP_HEAD ? "result: " + String(result) : "");
				return;
		}

		AsyncWebServerResponse *response;

		if (request->method() == HTTP_HEAD)
		{
			dataTimeOut = 0;
			response = request->beginResponse(200, "application/octet-stream", "");
			response->setContentLength(FinalDataSize);
		}
		else
		{
			dataTimeOut = millis();
			eMasterStatus = Busy_;
			onValueUpdate(ESP_Suffix_Status, eMasterStatus);

			response = request->beginResponse("application/octet-stream", CurrentDataSize, [&](uint8_t *writeBuffer, size_t maxWriteLength, size_t totalWritten) -> size_t {
				U32 writtenLength = 0;

				if (!BulkDetected) {
					LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

					Cancel_FileReceiveOperation(true);
					return writtenLength;
				}

				while (!writtenLength)
				{
					SerialPortReceiveProcess();

					while (GetBulkRemain() > 0 && writtenLength < maxWriteLength) {
						writeBuffer[writtenLength++] = ReadFromBulk();
						SerialPortReceiveProcess();
					}

					const bool isTimeout = (U32)(millis() - dataTimeOut) >= 2*validTimeOut_;

					if (CurrentDataSize && BulkTotalRead >= CurrentDataSize) {
						eOldMasterStatus = 0xff;
						dataTimeOut = 0;
						BulkReset(0);
					}

					else if (isTimeout || BulkOverflow || eProcessState != WaitforFile_) {
						LogMessageInFile("Cancelling File Receive Operation (terminated = false)");

						Cancel_FileReceiveOperation(false);
						return writtenLength;
					}

					else if ((U32)(millis() - dataTimeOut) >= validTimeOut_) break;
				}

				return writtenLength;
			});

			client->onPoll([&](void *arg, AsyncClient *client) {
				SerialPortReceiveProcess();

				const bool isTimeout = (U32)(millis() - dataTimeOut) >= 2*validTimeOut_;

				if (BulkDetected && client->connected() && !isTimeout) return;

				#ifdef DebugTools
				if (!client->connected()) WriteToDebug("\nClient Disconnected!!!\n");
				#endif

				LogMessageInFile("Cancelling File Receive Operation (terminated = false)");

				Cancel_FileReceiveOperation(false);
				if (client->connected()) client->stop();
			}, NULL);

			request->onDisconnect([&]() {
				LogMessageInFile("Cancelling File Receive Operation (terminated = true)");

				Cancel_FileReceiveOperation(true);
				if (BulkOverflow) ws.textAll("msg:5,overflow"); // inform of overflow if `BulkOverflow` was set
			});

			if (!CurrentDataSize) {
				// don't send content length as it's unknown
				// response->sendContentLength(false); // internal call already performed
			}
		}

		if (request->method() != HTTP_HEAD && request->hasHeader(F("Range")))
		{
			response->setCode(206); // Partial Content
			response->addHeader("Content-Range", "bytes " + String(start) + "-" + String(end) + "/" + (!total || total == 0xffffffff ? "" : String(total)));
		}

		if (ResourceDateTime != 0 && ResourceDateTime != ErrorNum_)
		{
			// Extract file date and time from ResourceDateTime
			const U16	file_date = (U16)((ResourceDateTime >> 16) & 0xFFFF),
					file_time = (U16) (ResourceDateTime        & 0xFFFF);

			tm timeinfo = {
				// Decode file time
				.tm_sec   =  (file_time & 0x1F) * 2,		// file_second
				.tm_min   =  (file_time >>  5)  & 0x3F,		// file_minute
				.tm_hour  =  (file_time >> 11),			// file_hour

				// Decode file date
				.tm_mday  =  (file_date       & 0x1F),		// file_day
				.tm_mon   = ((file_date >> 5) & 0x0F) - 1,	// file_month - 1
				.tm_year  = ((file_date >> 9) + 1980) - 1900,	// file_year  - 1900

				// Other fields will be calculated by mktime()
				.tm_wday  = 0,					// day of week
				.tm_yday  = 0,					// day of year

				.tm_isdst = -1,					// daylight saving time
			};

			mktime(&timeinfo);

			char buffer[30];
			// snprintf(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT",
			// dayNames[timeinfo.tm_wday], timeinfo.tm_mday, monthNames[timeinfo.tm_mon], timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
			strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);

			response->addHeader("Last-Modified", buffer);
		}

		if (basename.length() > 0)
		{
			strncpy(E_FileName, basename.c_str(), sizeof(E_FileName) - 1);
			E_FileName[sizeof(E_FileName) - 1] = Null_;
		}

		response->addHeader("Accept-Ranges", "bytes");
		response->addHeader("Content-Disposition", "attachment; filename=\"" + String(getBasename(E_FileName)) + "\"");
		request->send(response);
	});

	// Firmware download endpoint
	server->on("/firmware/download", HTTP_HEAD|HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		// ——————————————————————————————————————————————————————
		// 1. Decide dump scope: full flash vs. sketch only
		// ——————————————————————————————————————————————————————

		// Determine whether to dump full flash or just sketch
		bool dumpFull = request->hasArg("full") && request->arg("full") == "true";

		uint32_t totalSize = dumpFull ? ESP.getFlashChipSize()	// entire flash
					      : ESP.getSketchSize();	// only the running sketch

		uint32_t startByte = 0, endByte = totalSize - 1;

		// Choose filename
		String filename = dumpFull ? "flash_dump.bin" : "firmware.bin";

		// ——————————————————————————————————————————————————————
		// 2. Parse `Range` header if preset (skip for HEAD) : bytes=startByte-endByte
		// ——————————————————————————————————————————————————————

		bool isRangeRequest = false;

		// Check for HTTP Range header (e.g. bytes=100-200)
		if (request->hasHeader(F("Range")) && request->method() != HTTP_HEAD) {
			isRangeRequest = true;

			String range = request->getHeader("Range")->value();
			range.toLowerCase();

			// Must be in the form "bytes=X-Y"
			if (!range.startsWith("bytes=")) {
				sendError(request, 416, "Unsupported range format");
				return;
			}

			bool fromEnd = false;

			range.remove(0, 6); // Remove 'bytes='

			int dashPos = range.indexOf('-');
			if (dashPos < 0) {
				sendError(request, 416, "Invalid range syntax");
				return;
			}

			if (dashPos != -1 && range.length() > 1) {
				String strStart = range.substring(0, dashPos);
				String strEnd   = range.substring(   dashPos + 1);

				startByte = strStart.isEmpty() ?             0 : (uint32_t) strStart.toInt();
				endByte   =   strEnd.isEmpty() ? totalSize - 1 : (uint32_t) strEnd.toInt();

				fromEnd = strStart.isEmpty() && !strEnd.isEmpty();
			}

			// e.g. "Range: bytes=-N" → means last N bytes
			if (fromEnd) {
				startByte = totalSize - endByte;
				endByte = totalSize - 1;
			}

			// Validate specified range
			if (startByte > endByte || startByte >= totalSize) {
				sendError(request, 416, "Invalid range specified");
				return;
			}
		}

		// Total number of bytes we will send
		uint32_t contentLength = isRangeRequest ? (endByte - startByte + 1) : totalSize;

		// ——————————————————————————————————————————————————————
		// 3. Build the response (HEAD vs. GET/POST bodies)
		// ——————————————————————————————————————————————————————

		// Create response
		AsyncWebServerResponse* response;

		if (request->method() == HTTP_HEAD) {
			// HEAD: Only send headers with no body
			response = request->beginResponse(200, "application/octet-stream", "");
			response->setContentLength(contentLength);
		} else {
			// GET/POST: stream flash contents as binary data via callback
			response = request->beginResponse("application/octet-stream", contentLength,
							// This lambda will be called repeatedly to fill outgoing TCP buffers
							[startByte, totalSize](uint8_t* writeBuffer, size_t maxWriteLength, size_t offset) -> size_t {
								uint32_t addr = startByte + offset;				// Address to read from
								size_t remaining = totalSize - offset;				// Remaining bytes to read
								size_t toRead = min(remaining, maxWriteLength);			// Bytes to read in this iteration

								// Check if we are going to read past TAIL_SIZE from the end of the flash
								if (addr >= ESP.getFlashChipSize() - TAIL_SIZE) {
									size_t chunk = min(toRead, (size_t)TAIL_SIZE);
									return readFlashTail(writeBuffer, chunk);		// Read from flash tail into the provided buffer
								}

								return ESP.flashRead(addr, writeBuffer, toRead) ? toRead : 0;	// Read from flash memory into the provided buffer, return number of bytes read
							});
		}

		// If partial content was requested, add appropriate range headers
		if (isRangeRequest && request->method() != HTTP_HEAD) {
			response->setCode(206); // Partial Content
			response->addHeader("Content-Range", "bytes " + String(startByte) + "-" + String(endByte) + "/" + String(totalSize));
		}

		// ——————————————————————————————————————————————————————
		// 4. Set headers that are common to all request types
		// ——————————————————————————————————————————————————————

		// Add common response headers
		response->addHeader("Accept-Ranges", "bytes");
		response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");

		// Add headers specific to sketch-only dump
		if (!dumpFull && !isRangeRequest) {
			char buffer[40], md5[16];

			if (parseHexString(ESP.getSketchMD5(), md5))
			{
				base64_encodestate _state;
				base64_init_encodestate(&_state);
				int len = base64_encode_block((const char *)md5, sizeof(md5), buffer, &_state);
				    len = base64_encode_blockend((buffer + len), &_state);
				response->addHeader("Content-MD5", String(buffer));
			}

			char mon[4];
			int d, y, hh, mm, ss;
			bool valid = sscanf(__DATE__, "%3s %d %d", mon, &d, &y)		// "Mmm dd yyyy"
				  && sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);	// "hh:mm:ss"

			int m = monthStringToInt(mon);

			if (valid && is_between(m, 0, 11))
			{
				int wday = dayOfWeek(y, m+1, d);

				snprintf(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT", dayNames[wday], d, monthNames[m], y, hh, mm, ss);
				response->addHeader("Last-Modified", buffer);
			}
		}

		// ——————————————————————————————————————————————————————
		// 5. Send the response
		// ——————————————————————————————————————————————————————
		request->send(response);
	});

	#ifdef DebugTools
	server->on("/debug/info", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain",
			"Used Size:  " + String(DebugCounter) + "\n"
			"Total Size: " + String(DEBUG_SIZE) + "\n"
			"Last Reset: " + ESP.getResetInfo() + "\n"
		);
	});

	server->on("/debug/data", HTTP_GET, [&](AsyncWebServerRequest *request) {
		if (DebugCounter == 0)
		{
			request->send(404, "text/plain", "(empty)");
			return;
		}
		request->send(200, "text/plain", DebugData);
	});

	server->on("/debug/clear", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		DebugCounter = 0;
		DebugLastDirection = 0xff;
		memset(DebugData, Null_, sizeof(DebugData));
		request->send(200, "text/plain", "cleared");
	});

	server->on("/eProcessState", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", "eProcessState = " + String(eProcessState) + "\nMasterStatus = " + String(eMasterStatus));
	});

	server->on("/eResponseStatus", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", "eResponseSuffix = " + String(eResponseSuffix, 16) + "\neResponseCode = " + String(eResponseCode, 16));
	});

	server->on("/crash", HTTP_GET, [&](AsyncWebServerRequest *request) {
		// Crash
		int *p = (int*)0x00000000; *p = 0;
		// Divide by zero
		int a = 1, b = 0; a = a / b;
		// Starve WDT
		while (1) { }
	});
	#endif

	server->on("/datetime/get", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const bool success = ReadEPfromPU_DateTime();
		eDateTimeReady = false;

		if (success != OK_)
		{
			request->send(200, "text/plain", "fail");
			return;
		}

		AsyncResponseStream *response = request->beginResponseStream("text/plain");
		response->printf("%04d/%02d/%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
		request->send(response);
	});

	server->on("/datetime/set", HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("val")))
		{
			request->send(400, "text/plain", "missing `val` argument");
			return;
		}

		const String arg = request->arg(F("val"));

		if (!parseDateTime(arg, year, month, day, hour, minute, second))
		{
			request->send(400, "text/plain", "invalid date time");
			return;
		}

		const bool success = WriteEPtoPU_DateTime();

		request->send(200, "text/plain", success == OK_ ? "ok!" : "fail");
	});

	#ifdef DebugTools
	server->on("/datetime/parse", HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("val")))
		{
			request->send(400, "text/plain", "missing `val` argument");
			return;
		}

		const String arg = request->arg(F("val"));

		TimeStamp ts;

		if (!parseDateTime(arg, ts))
		{
			request->send(400, "text/plain", "invalid date time");
			return;
		}

		request->send(200, "text/plain", ts.toString() + "\n" +
			"epoch: " + String(ts.toEpoch()) + "\n");
	});

	server->on("/datetime/active", HTTP_GET, [&](AsyncWebServerRequest *request) {
		TimeStamp ts = PU_DateTime.adjusted();

		if (!ts.isValid())
		{
			request->send(500, "text/plain", "invalid date time");
			return;
		}

		request->send(200, "text/plain", ts.toString() + "\n" + "epoch: " + String(ts.toEpoch()) + "\n");
	});
	#endif

	/*
	server->on("/ntp/get", HTTP_GET, [&](AsyncWebServerRequest *request) {
		if (!timeClient.forceUpdate())
		{
			request->send(500, "text/plain", "failed to retrieve ntp time");
			return;
		}

		if (!timeClient.isTimeSet())
		{
			request->send(500, "text/plain", "ntp time not set yet");
			return;
		}

		// time_t epochTime = timeClient.getEpochTime();
		// struct tm *ptm = gmtime((time_t*) &epochTime);

		// hour == timeClient.getHours()
		// minute == timeClient.getMinutes()

		// https://github.com/SensorsIot/SNTPtime/blob/master/SNTP.cpp
		// https://github.com/esp8266/Arduino/blob/master/libraries/esp8266/examples/NTP-TZ-DST/NTP-TZ-DST.ino

		request->send(200, "text/plain", String(timeClient.getFormattedTime()));
	});
	*/

	server->on("/wifi/signal", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const S8 rssi = wifi_station_get_rssi();

		String str = "Invalid";

		if (rssi != 31)
		{
			if (rssi <= -110) str = "No signal"; else
			if (rssi <= -80)  str = "Bad";       else
			if (rssi <= -70)  str = "Low";       else
			if (rssi <= -60)  str = "Normal";    else
			if (rssi <= -50)  str = "Good";      else
			if (rssi <= -40)  str = "Excellent"; else
			str = "UnDefined";
		}

		request->send(200, "text/plain", String(rssi) + "\n" + str);
	});

	server->on("/wifi/country", HTTP_GET, [&](AsyncWebServerRequest *request) {
		wifi_country_t country;
		memset(country.cc, Null_, sizeof(country.cc));
		wifi_get_country(&country); country.cc[2] = Null_;
		request->send(200, "text/plain", "country: " + isprint(country.cc[0]) ? String(country.cc) : String("(unknown)") + "\n");
	});

	server->on("/wifi/status", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain",
			"ap status: " + String(getAPStatus()) + "\n"
			"st status: " + String(getStationStatus()) + "\n"
		);
	});

	server->on("/config/ap", HTTP_GET, [&](AsyncWebServerRequest *request) {
		bool result = true;

		struct softap_config config;
		result &= wifi_softap_get_config(&config);

		struct ip_info ip;
		result &= wifi_get_ip_info(SOFTAP_IF, &ip);

		if (!result)
		{
			request->send(200, "text/plain", "fail");
			return;
		}

		request->send(200, "text/plain",
			"ssid: " + String((C8*)config.ssid) + "\n"
			"pass: " + String((C8*)config.password) + "\n"
			"ip: " + IPAddress(ip.ip.addr).toString() + "\n"
			"netmask: " + IPAddress(ip.netmask.addr).toString() + "\n"
		);
	});

	server->on("/config/st", HTTP_GET, [&](AsyncWebServerRequest *request) {
		bool result = true;

		struct station_config config;
		result &= wifi_station_get_config(&config);

		struct ip_info ip;
		result &= wifi_get_ip_info(STATION_IF, &ip);

		auto dns1 = dns_getserver(0),
		     dns2 = dns_getserver(1);

		if (!result)
		{
			request->send(200, "text/plain", "fail");
			return;
		}

		request->send(200, "text/plain",
			"ssid: " + String((C8*)config.ssid) + "\n"
			"pass: " + String((C8*)config.password) + "\n"
			"dhcp: " + String(wifi_station_dhcpc_status() == DHCP_STARTED ? "enabled" : "disabled") + "\n"
			"ip: " + IPAddress(ip.ip.addr).toString() + "\n"
			"netmask: " + IPAddress(ip.netmask.addr).toString() + "\n"
			"gw: " + IPAddress(ip.gw.addr).toString() + "\n"
			"dns: " + IPAddress(dns1).toString() + ", " + IPAddress(dns2).toString() + "\n"
		);
	});

	server->on("/hostname/check", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("value")))
		{
			request->send(400, "text/plain", "missing `value` argument");
			return;
		}

		const String arg = request->arg(F("value"));

		const C8* hostname = arg.c_str();

		if (!isValidHostName(hostname))
		{
			request->send(400, "text/plain", "invalid hostname provided");
			return;
		}

		IPAddress ip;

		const bool success = WiFi.hostByName(hostname, ip);

		if (success != OK_)
		{
			request->send(400, "text/plain", "hostname not found");
		}

		request->send(200, "text/plain", ip.toString());
	});

	#ifdef DebugTools
	// server->on("/login", HTTP_POST, [&](AsyncWebServerRequest *request) {
	// 	LoginHandler::handleLogin(request);
	// });

	server->on("/auth_dump", HTTP_ANY, [&](AsyncWebServerRequest *request) {
		String result = "";

		const char* attributes[] = {"username", "password", "token", "session_id"};

		for (const char* attr : attributes) {
			if (request->hasAttribute(attr)) {
				result += String(attr) + ": " + String(request->getAttribute(attr)) + "\n";
			}
		}

		request->send(200, "text/plain", result);
	});

	server->on("/sessions_dump", HTTP_ANY, [&](AsyncWebServerRequest *request) {
		String result = "";

		for (unsigned int i = 0; i < count_of(sessions); i++)
		{
			if (sessions[i].accessLevel != UnknownLevel_) {
				result +=
					"Token: " + convertToHexString(sessions[i].authToken, sizeof(sessions[i].authToken)) + "\n"
					"Level: " + String(sessions[i].accessLevel) + "\n"
					"Is Logged In: " + String(sessions[i].isLoggedIn ? "yes" : "no") + "\n"
					"Last Active: " + String(sessions[i].lastActive) + "\n"
					"----\n";
			}
		}

		request->send(200, "text/plain", result);
	});

	server->on("/random", HTTP_GET, [&](AsyncWebServerRequest *request) {
		char token_bytes[TOKEN_LENGTH / 2];

		// fill token_bytes with random data
		for (size_t i = 0; i < sizeof(token_bytes);) {
			// add 4 bytes from RANDOM_REG32 to token_bytes
			const uint32_t randomValue = ESP.random();
			memcpy(token_bytes + i, &randomValue, min(sizeof(randomValue), sizeof(token_bytes) - i));
			i += min(sizeof(randomValue), sizeof(token_bytes) - i);
		}

		request->send(200, "text/plain", convertToHexString(token_bytes, sizeof(token_bytes)));
	});
	#endif

	server->on("/username", HTTP_GET, [&](AsyncWebServerRequest *request) {
		if (eMasterStatus == Busy_)
		{
			request->send(503, "text/plain", "device is busy");
			return;
		}

		const bool result = ReadEPfromPU_Str(ESP_Suffix_UserName) && ReadEPfromPU_Num(ESP_Suffix_AccessLevel) && ReadEPfromPU_Num(ESP_Suffix_IsExecutable);

		if (result != OK_)
		{
			request->send(400, "text/plain", "fail");
			return;
		}

		const bool isUserNameSet = strlen(E_UserName) > 0 && strcmp(E_UserName, "--") != 0;

		// const bool noAccess = !AuthenticationHandler::hasAccess(E_AccessLevel);

		request->send(200, "text/plain", (isUserNameSet ? urlEncode(String(E_UserName)) : "--") + "\n" + String(E_AccessLevel) + "\n" + String(IsExecutable));
	});

	server->on("/lang", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", E_SystemLanguage == result_Undefined ? "fail" : String(E_SystemLanguage));
	});

	server->on("/lang", HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("lang")))
		{
			request->send(400, "text/plain", "missing `lang` argument");
			return;
		}

		const String arg = request->arg(F("lang"));

		const C8 lang = arg.charAt(0);

		if (lang != 'E' && lang != 'F' && lang != 'A' && lang != 'R')
		{
			request->send(400, "text/plain", "invalid lang");
			return;
		}

		const bool result = Request_SetSystemLanguage(lang);

		request->send(200, "text/plain", result == OK_ ? "ok!" : "fail");
	});

	server->on("/pu", HTTP_GET, [&](AsyncWebServerRequest *request) {
		bool result = NotOK_;

		eMasterStatus = Request_GivePUStatus();

		if (eMasterStatus == Ready_ || eMasterStatus == Booting_)
		{
			if (E_SerialNumber >= UnDefinedNum_ || E_SystemLanguage == result_Undefined || eMasterStatus == Booting_)
			{
				E_SerialNumber = UnDefinedNum_;
				result = Request_GiveBasicData();
			}
			else
			{
				result = OK_;
			}

			if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0)
				ReadEPfromPU_Str(ESP_Suffix_MainVersion);
		}

		if (result != OK_ && eMasterStatus == Ready_)
		{
			eMasterStatus = Busy_;
			E_SerialNumber = UnDefinedNum_;
			ReadEPfromPU_Num(ESP_Suffix_SerialNumber);
			onValueUpdate(ESP_Suffix_Status, eMasterStatus);
		}

		request->send(200, "text/plain",
			(eMasterStatus == Ready_ ? String("Ready") : (eMasterStatus == Booting_ ? String("Booting") : (eMasterStatus == Busy_ ? String("Busy") : String("Unknown")))) + "\n" +
			(E_SerialNumber >= UnDefinedNum_ ? String("fail") : String(E_SerialNumber)) + "\n" +
			(E_MainVersion));
	});

	server->on("/pu_reboot", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		const U8 result = Request_RebootPU();

		switch (result)
		{
			case result_Fail:		request->send(500, "text/plain", "fail");		return;
			case result_Success:		request->send(200, "text/plain", "ok!");		return;
			case result_Unauthorised: 	request->send(403, "text/plain", "forbidden");		return;
			case result_NotAcceptable:	request->send(403, "text/plain", "not acceptable");	return;
		}

		if (result == 0xff) {
			request->send(500, "text/plain", "device unavailable");
			return;
		}

		request->send(500, "text/plain", "result: " + String(result));
	});

	server->on("/pu_reset", HTTP_GET|HTTP_POST, [&](AsyncWebServerRequest *request) {
		digitalWrite(MRST, LOW);
		pinMode(MRST, OUTPUT);
		delay(100);
		pinMode(MRST, INPUT);
		digitalWrite(MRST, HIGH);

		request->send(500, "text/plain", "ok!");
	});

	server->on("/is_exec", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const bool result = ReadEPfromPU_Num(ESP_Suffix_IsExecutable);
		request->send(200, "text/plain", result == OK_ ? String(IsExecutable) : "fail");
	});

	server->on("/page_id", HTTP_GET, [&](AsyncWebServerRequest *request) {
		const bool result = ReadEPfromPU_Num(ESP_Suffix_IDIndex);
		request->send(200, "text/plain", result == OK_ ? String(E_IDIndex) : "fail");
	});

	server->on("/page_goto", HTTP_POST, [&](AsyncWebServerRequest *request) {
		if (!request->hasArg(F("pu_id")))
		{
			request->send(400, "text/plain", "missing `page` argument");
			return;
		}

		const U32 id = request->arg(F("pu_id")).toInt();

		const bool result = Request_GoToChart(id);
		request->send(200, "text/plain", result == OK_ ? "ok!" : "fail");
	});

	server->on("/update/info", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "application/json",
			"{\"hash\": \"" + String(ESP.getSketchMD5()) + "\",\n"
			"\"size\": " + String(ESP.getSketchSize()) + ",\n"
			"\"free\": " + String(ESP.getFreeSketchSpace()) + ",\n"
			"\"date\": \"" + __DATE__ " " __TIME__ + "\"}"
		);
	});

	server->on("/info", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain",
			"hostname: " + String(WiFi.hostname()) + "\n"
			"wifi version: " + String(ESP.getFullVersion()) + "/WebServer:" ASYNCWEBSERVER_VERSION + "\n"
			"esp chip id: " + String(ESP.getChipId(), HEX) + "\n"
			"flash chip id: " + String(ESP.getFlashChipId(), HEX) + "\n"
			"firmware hash: " + String(ESP.getSketchMD5()) + (!firmwareIsValid ? " (CORRUPTED!)" : "") + "\n"
			"protocol rev.: " + String(ESP_ProtocolRevision_) + (RevisionMatch ? "" : " (no match)") + "\n"
			"st mac: " + WiFi.macAddress() + "\n"
			"ap mac: " + WiFi.softAPmacAddress() + "\n"
			"client ip: " + request->client()->remoteIP().toString() + "\n"
			"flash size: " + humanReadableBytes(ESP.getFlashChipSize()) + (ESP.getFlashChipSize() != ESP.getFlashChipRealSize() ? " (real: " + humanReadableBytes(ESP.getFlashChipRealSize()) + ")" : "") + "\n"
			// "flash speed: " + String(ESP.getFlashChipSpeed() / 1000000) + " MHz\n" "flash mode: " + String(ESP.getFlashChipMode()) + "\n"
			"program usage: " + String((ESP.getSketchSize() * 100) / (ESP.getSketchSize() + ESP.getFreeSketchSpace())) + "%\n"
			"settings hash: " + String(flashSettings.crc32, HEX) + (!settingsError ? "" : ", status: " + String(settingsError)) + "\n"
			"heap memory: " + String(ESP.getFreeHeap()) + "\n"
			#ifdef DebugTools
			"debug tools: enabled\n"
			"debug buffer: " + String(DebugCounter) + " of " + String(DEBUG_SIZE) + " bytes\n"
			#endif
			"build: " __DATE__ " " __TIME__ " by " BUILD_USERNAME "@" BUILD_HOSTNAME "\n"
			"uptime: " + getFormattedUptime(millis() / 1000) + "\n"
			"\n"
		);
	});

	// Route for root index / web page
	server->on("/", HTTP_GET, [&](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", index_content);
		// request->send_P(200, "text/plain", index_content, processor);
		// request->send(LittleFS, "/index.html", "text/html");
	});

	// server->serveStatic("/", LittleFS, "/"); // .setDefaultFile("index.html")

	// SPIFFS.begin();
	// LittleFS.begin();

	server->addMiddleware(&cors);
	cors.setOrigin("*");
	cors.setMethods("GET, POST, OPTIONS");
	cors.setHeaders("*"); // "Content-Type, Authorization, X-Requested-With"
	cors.setMaxAge(600);

	DefaultHeaders::Instance().addHeader("Cache-Control", "no-cache");
	DefaultHeaders::Instance().addHeader("Server", "PU850");

	server->addHandler(&loginHandler);
	server->addMiddlewares({&authMiddleware, &authzMiddleware});

	// server->catchAllHandler().skipServerMiddlewares();

	auto catchAllHandler = &server->catchAllHandler();

	catchAllHandler->skipServerMiddlewares(); // skip authorization on non-existing routes

	server->begin();

	Telnet_Setup();

	#ifdef ShellOnSerial
	Shell_clearScreen();
	Serial.print(ansi.setFG(ANSI_BRIGHT_GREEN));
	Serial.print("\nWelcome to " + WiFi.hostname() + "!\n");
	Serial.print(ansi.reset());
	newPrompt(0);
	initStatus = true;
	return;
	#endif

	#ifdef DebugTools
	#ifdef ShellOnSerial
	WriteToDebug("(ShellOnSerial enabled)\n");
	#endif
	#ifdef DebugOnSerial
	WriteToDebug("(DebugOnSerial enabled)\n");
	#endif
	#endif

	delay(250); // wait for PU master to assume that the ESP boot has finished (200ms)

	if (InitializeUARTService())
	{
		initStatus = true;
	}

	oldNetStatus = 0xff;
}

void wifi_handle_event_cb(System_Event_t *evt)
{
	switch (evt->event)
	{
		case EVENT_OPMODE_CHANGED:
			os_printf("opmode changed: %d -> %d\n",
				evt->event_info.opmode_changed.old_opmode,
				evt->event_info.opmode_changed.new_opmode);
			WriteEPtoPU_Num(ESP_Suffix_NetFlags);
			break;

		// We have connected to an access point.
		case EVENT_STAMODE_CONNECTED:
			os_printf("connected to ssid %s, channel: %d, bssid: " MACSTR "\n",
				evt->event_info.connected.ssid,
				evt->event_info.connected.channel,
				MAC2STR(evt->event_info.connected.bssid));
			isDHCPTimeout = false;
			memcpy(BSSID, evt->event_info.connected.bssid, sizeof(BSSID));
			WriteEPtoPU_Num(ESP_Suffix_DHCP_Status);
			break;

		// We have disconnected or been disconnected from an access point.
		case EVENT_STAMODE_DISCONNECTED:
			os_printf("disconnected from ssid %s, reason: %d\n",
				evt->event_info.disconnected.ssid,
				evt->event_info.disconnected.reason);
			char ssid[33];
			memset(ssid, Null_, sizeof(ssid));
			memcpy(ssid, evt->event_info.disconnected.ssid, evt->event_info.disconnected.ssid_len);
			memset(BSSID, Null_, sizeof(BSSID));
			WriteEPtoPU_Num(ESP_Suffix_NetStatus);
			break;

		// The authentication information at the access point has changed.
		case EVENT_STAMODE_AUTHMODE_CHANGE:
			os_printf("sta auth mode changed: %d -> %d\n",
				evt->event_info.auth_change.old_mode,
				evt->event_info.auth_change.new_mode);
			break;

		// We have been allocated an IP address.
		case EVENT_STAMODE_GOT_IP:
			os_printf("got sta ip: " IPSTR ", netmask: " IPSTR ", gw: " IPSTR "\n",
				IP2STR(&evt->event_info.got_ip.ip),
				IP2STR(&evt->event_info.got_ip.mask),
				IP2STR(&evt->event_info.got_ip.gw));

			isDHCPTimeout = false;
			WriteEPtoPU_Num(ESP_Suffix_DHCP_Status);

			WriteEPtoPU_Num(ESP_Suffix_Station_IP_Addr);
			WriteEPtoPU_Num(ESP_Suffix_Station_SubnetMask);
			WriteEPtoPU_Num(ESP_Suffix_Station_GateWay);

			WriteEPtoPU_Num(ESP_Suffix_Station_DNS_Addr1);
			WriteEPtoPU_Num(ESP_Suffix_Station_DNS_Addr2);
			break;

		case EVENT_STAMODE_DHCP_TIMEOUT:
			os_printf("dhcp timeout\n");
			isDHCPTimeout = true;
			// Request_SendMessage_ToPU("DHCP Timeout!", 0, 0, ID_ErrorIcon_);
			// wifi_station_dhcpc_stop();
			// WriteEPtoPU_Num(ESP_Suffix_NetFlags);
			// WriteEPtoPU_Num(ESP_Suffix_DHCP_Status);
			break;

		case EVENT_SOFTAPMODE_STACONNECTED:
			os_printf("AP: new device joined: " MACSTR ", AID = %d\n",
				MAC2STR(evt->event_info.sta_connected.mac),
				evt->event_info.sta_connected.aid);
			WriteEPtoPU_Num(ESP_Suffix_AP_DevicesConnected);
			break;

		case EVENT_SOFTAPMODE_STADISCONNECTED:
			os_printf("AP: device left: " MACSTR ", AID = %d\n",
				MAC2STR(evt->event_info.sta_disconnected.mac),
				evt->event_info.sta_disconnected.aid);
			WriteEPtoPU_Num(ESP_Suffix_AP_DevicesConnected);
			break;

		case EVENT_SOFTAPMODE_PROBEREQRECVED:
			os_printf("AP: probe request from station " MACSTR ", rssi = %d\n",
				MAC2STR(evt->event_info.ap_probereqrecved.mac), evt->event_info.ap_probereqrecved.rssi);
			break;

		case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
			os_printf("AP: device: " MACSTR " received ip: " IPSTR "\n",
				MAC2STR(evt->event_info.distribute_sta_ip.mac),
				IP2STR(&evt->event_info.distribute_sta_ip.ip));
			break;

		default:
			os_printf("Wifi: unexpected event %d\n", evt->event);
			break;
	}

	SendNetStatus_ToPU();

	// ServiceEvent(evt);
}

U8   getDHCPStatus()
{
	// 0: Disabled / 1: Acquiring… / 2: OK / 3: Timeout (Fail)     / 0xff : Undefined

	if (isDHCPTimeout)
	{
		return 3;
	}

	const dhcp_status status = wifi_station_dhcpc_status();

	if (status == dhcp_status::DHCP_STOPPED)
	{
		return 0;
	}

	const station_status_t wifi_status = wifi_station_get_connect_status();

	if (wifi_status == station_status_t::STATION_GOT_IP)
	{
		struct ip_info sta;
		wifi_get_ip_info(STATION_IF, &sta);

		if (sta.ip.addr != 0 && sta.ip.addr != 0xffffffff)
		{
			return 2;
		}
	}

	if (status == dhcp_status::DHCP_STARTED)
	{
		return 1;
	}

	return 0xff;
}

U32  getMacAddress(U8 interface, bool part)
{
	U8 mac[6];
	wifi_get_macaddr(interface, mac);
	return part == SecondPart_ ? PackU32(0, mac[5], mac[4], mac[3]) : PackU32(0, mac[2], mac[1], mac[0]);
}

bool setNetFlags(U8 NetFlags)
{
	bool success = true;

	// bits:  0: STA Enabled | 1: AP Enabled | 2: DHCP Enabled | 3:  | 4:

	U8 wifi_mode = wifi_get_opmode();

	if (BitIsSet(NetFlags, 0))
		SetMode  (wifi_mode, WIFI_STA);
	else	ClearMode(wifi_mode, WIFI_STA);

	if (BitIsSet(NetFlags, 1))
		SetMode  (wifi_mode, WIFI_AP);
	else	ClearMode(wifi_mode, WIFI_AP);

	// success &= WiFi.enableSTA( BitIsSet(NetFlags, 0) );
	// success &= WiFi.enableAP ( BitIsSet(NetFlags, 1) );

	success &= WiFi.mode((WiFiMode_t) wifi_mode); // WIFI_AP_STA

	const bool sta_enabled = (wifi_mode & WIFI_STA),
		   ap_enabled  = (wifi_mode & WIFI_AP);

	if (sta_enabled)
	{
		isDHCPTimeout = false;

		if (BitIsSet(NetFlags, 2))
			success &= wifi_station_dhcpc_start();
		else	success &= wifi_station_dhcpc_stop();
	}

	if (ap_enabled) { }

	flashSettings.net_flags = NetFlags;

	return success;
}

U32  getNetFlags()
{
	U8 NetFlags = 0;

	// bits:  0: STA Enabled | 1: AP Enabled | 2: DHCP Enabled | 3:  | 4:

	U8 wifi_mode = wifi_get_opmode();

	if (ModeIsSet(wifi_mode, WIFI_STA))
		SetBit  (NetFlags, 0);
	else	ClearBit(NetFlags, 0);

	if (ModeIsSet(wifi_mode, WIFI_AP))
		SetBit  (NetFlags, 1);
	else	ClearBit(NetFlags, 1);

	switch (wifi_station_dhcpc_status())
	{
		case DHCP_STARTED: SetBit  (NetFlags, 2); break;
		case DHCP_STOPPED: ClearBit(NetFlags, 2); break;
	}

	return NetFlags;
}

U8   getAPStatus()
{
	const U8 mode = wifi_get_opmode();

	if ( (mode & WIFI_AP) != 0 )
		return NetStatus_OK_;

	return NetStatus_Disconnected_;
}

U8   getStationStatus()
{
	const station_status_t status = wifi_station_get_connect_status();

	switch (status)
	{
		case STATION_GOT_IP:		return NetStatus_OK_;			break;	// WL_CONNECTED
		case STATION_CONNECTING:	return NetStatus_Connecting_;		break;	// WL_CONNECTED
		case STATION_WRONG_PASSWORD:	return NetStatus_WrongPassword_;	break;	// WL_WRONG_PASSWORD;
		case STATION_NO_AP_FOUND:	return NetStatus_NotFound_;		break;	// WL_NO_SSID_AVAIL;
		case STATION_CONNECT_FAIL:	return NetStatus_ConnectFail_;		break;	// WL_CONNECT_FAILED;
		case STATION_IDLE:		return NetStatus_Idle_;			break;	// WL_IDLE_STATUS;
		default:			return NetStatus_Disconnected_;		break;	// WL_DISCONNECTED
	}

	return NetStatus_UnDefined_;
}

void onDateTimeReceived()
{
	// https://github.com/esp8266/Arduino/blob/master/libraries/esp8266/examples/RTCUserMemory/RTCUserMemory.ino
	// |<------system data (256 bytes)------->|<-----------------user data (512 bytes)--------------->|
	// OTA takes the first 128 bytes of the USER area.
	// if(!ESP.rtcUserMemoryRead(RTC_USER_DATA_ADDR, &data, sizeof(data))) {}
	// if(!ESP.rtcUserMemoryWrite(RTC_USER_DATA_ADDR, &data, sizeof(data))) {}
	// The offset is measured in blocks of 4 bytes and can range from 0 to 127 blocks (total size of RTC memory is 512 bytes).
	// The data should be 4-byte aligned.
	// Data stored in the first 32 blocks will be lost after performing an OTA update, because they are used by the Core internals.
	// TODO: Implement RTC memory for storing device date/time

	PU_DateTime.set(year, month, day, hour, minute, second);
}

void onDateTimeSent()
{
	PU_DateTime.set(year, month, day, hour, minute, second);
	eDateTimeReady = true;
}

void onValueUpdate(U8 Suffix, U32 Value)
{
	switch (Suffix)
	{
		case ESP_Suffix_Status:
			lastMasterStatus = Value;
			ws.printfAll("status:%u", (U8)Value);
			break;

		case ESP_Suffix_HostName:
			WiFi.setHostname(flashSettings.hostname);
			MDNS.setHostname(flashSettings.hostname);
			NBNS.begin(flashSettings.hostname);
			LLMNR.begin(flashSettings.hostname);
			SSDP.setName(flashSettings.hostname);
			ws.printfAll("host:%s", flashSettings.hostname);
			break;

		case ESP_Suffix_SerialNumber:
			ws.printfAll("SN:%lu", Value);
			SSDP.setSerialNumber(E_SerialNumber);
			break;

		case ESP_Suffix_IDIndex:
			ws.printfAll("page:%u", (U8)Value);
			break;

		case ESP_Suffix_UserName:
			ws.printfAll("user:%s", urlEncode(String(E_UserName)).c_str());
			break;

		case ESP_Suffix_AccessLevel:
			ws.printfAll("level:%u", (U8)Value);
			break;

		case ESP_Suffix_SystemLanguage:
			ws.printfAll("lang:%c", (C8)E_SystemLanguage);
			break;

		case ESP_Suffix_IsExecutable:
			ws.printfAll("is_exec:%u", (U8)Value);
			break;

		case ESP_Suffix_DateTimeInfo:
			ws.printfAll("%04d/%02d/%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
			break;

		case ESP_Suffix_HideWeigh:
			ws.printfAll("hidden:%u", (U8)Value);
			break;

		case ESP_Suffix_Weigh:
			lastWeightReceived = millis();
			lastWsWeightValue = (S32)Value;
			lastWeightSent = millis();
			ws.textAll(Value == UnDefinedNum_ ? "off" : String((S32)Value));
			break;
	}
}

void onResponseReceived(U8 Prefix, U8 Suffix, U16 Response)
{
	switch (Prefix)
	{
		case ESP_Prefix_Request:
			switch (Suffix)
			{
				case ESP_Suffix_DateTimeInfo:
					if (Response != result_Success)
						break;
					onDateTimeSent();
					break;

				case ESP_Suffix_Tare:
					ws.printfAll("tare:%u", Response);
					break;

				case ESP_Suffix_MultiSettings:
					if (Response == result_Success)
						eMultiSettingsSent = true;
					break;
			}
			break;

		case ESP_Prefix_FileRequest:
			switch (Suffix)
			{
				case ESP_Suffix_SendFile:
					if (eResponsePrefix == ESP_Prefix_Request && eResponseSuffix == ESP_Suffix_Report)
						eResponseCode = Response;
					break;
			}
			break;
	}
}

void onBulkDataReceived()
{
	/*
	#ifdef DebugTools
	if (!BulkDetected)
		return;

	switch (ePrefix)
	{
		case ESP_Prefix_SetStr:
			telnet.printf("String EndPoint: %u\n", eSuffix);
			telnet.printf("Data: %s\n", PopDatStr);
			telnet.printf("\n");
			break;

		case ESP_Prefix_SendBulk:
			if (BulkTotalRead == 0)
			{
				telnet.println("Bulk Transfer Started!");
			}

			goto BulkDataReceived;

		case ESP_Prefix_FileOrder:

			if (BulkTotalRead == 0)
			{
				telnet.printf("Filename: %s\n", getBasename(E_FileName));
				telnet.printf("Size: %d bytes\n", CurrentDataSize);
			}

			BulkDataReceived:

			while (GetBulkRemain() > 0)
			{
				const C8 data = ReadFromBulk();

				telnet.print(data);

				// telnet.printf("%02X ", data);

				// if (BulkTotalRead % 16 == 0)
				// 	telnet.println();
			}

			if (FinalDataSize != 0)
			{
				telnet.println();
				telnet.printf("Final Size: %d bytes\n", FinalDataSize);

				if (BulkOverflow)
				{
					telnet.println("Bulk Overflow!!!");
				}

				BulkReset(0);
			}

			break;
	}
	#endif
	*/
}

void onMessageReceived(U8 id, U8 title, U8 buttons, U8 icon)
{
	String data = "";
	data.reserve(255);

	// format: msg:<icon>,0-254
	//         msg:<icon>,Estr

	if ( id == 0xff && strlen(E_MessageStr) > 0 )
	{
		data.concat(urlEncode(String(E_MessageStr)));
	}
	else
	{
		data.concat(id);
	}

	ws.printfAll("msg:%u,%s", (U8)icon, data.c_str());
}

void onBeepReceived(U32 num)
{
	ws.printfAll("beep:%u", (U8)num);
}

void onWsReceivedCommand(AsyncWebSocketClient *client, const C8* data)
{
	if (strcasecmp(data, "weight") == 0)
	{
		Request_GiveWeight();
		Request_IsWeightHidden();
		onValueUpdate(ESP_Suffix_Weigh, eWeightValue);
	}

	if (strcasecmp(data, "datetime") == 0)
	{
		ReadEPfromPU_DateTime();
	}

	// (TODO) Remove
	if (strcmp(data, "client:helloWorld") == 0)
	{
		// onBeepReceived(1);
		// PushToStrEndPoint(ESP_Suffix_MessageStr, ",v,n lhadk");
		// onMessageReceived(0xff, 0, 0, ID_InformationIcon_);

		client->text("server:PU850_OK");
	}
}

void ConfirmSettings(U8 save, U8 section)
{
	U8 result = result_Success;

	const U32 startMillis = millis();

	switch (save)
	{
		// Save Changes to flash
		case Yes_:
			if ( !Settings_Write() )
			{
				result = result_Fail;
				break;
			}
			SendResponseCode_ToPU(ESP_Prefix_Order, ESP_Suffix_ConfirmSettings, result_Processing);
			WiFi_Setup();
			break;

		// Discard (Restore from flash)
		case No_:
			SendResponseCode_ToPU(ESP_Prefix_Order, ESP_Suffix_ConfirmSettings, result_Processing);
			Settings_Read();
			WiFi_Setup();
			break;

		// No changes
		case Cancel_:
			oldNetStatus = 0xff; // Send net flags and status in `loop()` later
			return;

		default:
			result = result_NotAcceptable;
			break;
	}

	const U32 elapsed = millis() - startMillis;

	if (elapsed < 500)
	{
		delay(500 - elapsed);
	}

	SendResponseCode_ToPU(ESP_Prefix_Order, ESP_Suffix_ConfirmSettings, result);
}

void loop()
{

	if (!initStatus && (eMasterStatus == Ready_ || eMasterStatus == Booting_))
	{
		if (InitializeUARTService())
		{
			initStatus = true;
		}
		else if (eMasterStatus == Ready_)
		{
			eMasterStatus = Busy_;
			onValueUpdate(ESP_Suffix_Status, eMasterStatus);
		}
	}

	SerialPortReceiveProcess();

	/*
	if (WiFi.waitForConnectResult() != WL_CONNECTED) {
		return;
	}
	*/

	/*
	const U8 WiFiStatus = WiFi.status();

	if ( WiFiStatus != lastWiFiStatus )
	{
		lastWiFiStatus = WiFiStatus;
		onWiFiStatusChanged();

		if ( WiFiStatus == WL_CONNECTED ) {}
	}
	*/

	if ((U32)(millis() - last1sMillis) >= 1000L)
	{
		last1sMillis = millis();

		if (initStatus && eOldMasterStatus == 0xff && IsSerialIdle())
		{
			SendESPStatus_ToPU(LastESPStatus, true);
		}

		if (eMasterStatus == Ready_ && eWeightValue == UnDefinedNum_ && (U32)(millis() - lastWeightReceived) >= 1000L && ws.count() == 0)
		{
			SendCommand(ESP_Prefix_Request, ESP_Suffix_Weigh, Once_);
		}

		if (eMasterStatus == Busy_) {
			eWeightReady = false;
			eWeightValue = UnDefinedNum_;
		}

		// Send to app every 1 second (keepalive connection)
		if (!AsyncOTA.rebootRequired())
		{
			ESP.wdtFeed();

			if (updateProgress != 0xff)
				// Send update progress as keepalive
				ws.printfAll("update:%d%%", updateProgress);

			else if ((U32)(millis() - lastWeightSent) > 1000L)
				// Send weight as keepalive
				onValueUpdate(ESP_Suffix_Weigh, eWeightValue);
		}

		if (eMasterStatus == Ready_ && (E_IDIndex == WiFiStationStatusIndex_ || E_IDIndex == WiFiRemoteUserPageIndex_ || E_IDIndex == MainMenuIndex_ || E_IDIndex == 0) && WiFi.status() == WL_CONNECTED)
		{
			S8 rssiRaw = wifi_station_get_rssi();

			E_RSSI  = 0xff; // UnDefinedNum_;

			// 31 = failed
			if (rssiRaw != 31)
			{
				if (rssiRaw <= -110) E_RSSI = 0; else	// No signal
				if (rssiRaw <= -80)  E_RSSI = 1; else	// Bad
				if (rssiRaw <= -70)  E_RSSI = 2; else	// Low
				if (rssiRaw <= -60)  E_RSSI = 3; else	// Normal
				if (rssiRaw <= -50)  E_RSSI = 4; else	// Good
				if (rssiRaw <= -40)  E_RSSI = 5;	// Excellent
			}

			if (oldRSSI != E_RSSI)
			{
				oldRSSI = E_RSSI;
				WriteEPtoPU_Num(ESP_Suffix_Station_RSSI);
			}
		}

		if (!AsyncOTA.rebootRequired() && !Update.isRunning() && updateProgress == 0xff)
		{
			// ArduinoOTA.handle();

			// timeClient.update();

			// dnsServer.processNextRequest();

			if (mndpRunning) sendMNDP();
		}

		SerialPortReceiveProcess();

		return;
	}

	if ((U32)(millis() - last100msMillis) >= 100L)
	{
		last100msMillis = millis();

		if (updateProgress != 0xff)
		{
			if (updateProgress != lastUpdateProgress)
			{
				lastUpdateProgress = updateProgress;
				// ws.printfAll("update:%d%%", updateProgress);
				os_printf("\r\e[2KUpdate progress: %d%%", updateProgress);
			}
		}
		else
		{
			if (eWeightReady && lastWsWeightValue != eWeightValue)
			{
				// lastWeightSent = 0;
				// lastWsWeightValue = ErrorNum_;
				onValueUpdate(ESP_Suffix_Weigh, eWeightValue);
				eWeightReady = false;
			}

			if (eDateTimeReady)
			{
				onValueUpdate(ESP_Suffix_DateTimeInfo, 0);
				eDateTimeReady = false;
			}

			if (eMasterStatus == Ready_)
			{
				U16 wsCount = ws.count();

				if (wsCount != lastWsCount)
				{
					lastWsCount = wsCount;
					ReadEPfromPU_Num(ESP_Suffix_IDIndex);
					ReadEPfromPU_Num(ESP_Suffix_IsExecutable);
					Request_SendingWeight(eSendingWeigh = (wsCount > 0));
					WriteEPtoPU_Num(ESP_Suffix_AppRunningCount);
				}

				if (lastMasterStatus != Ready_)
					onValueUpdate(ESP_Suffix_Status, eMasterStatus);

				if (oldNetStatus == 0xff) {
					WriteEPtoPU_Num(ESP_Suffix_NetFlags);
					SendNetStatus_ToPU();
				}

				if (!resetChecked) {
					resetChecked = true;

					const rst_info* resetInfo = system_get_rst_info();
					os_printf("last reset reason: %x\n", resetInfo->reason);

					switch (resetInfo->reason)
					{
						case REASON_DEFAULT_RST:		// Power On or Hard Reset
						break;

						case REASON_SOFT_RESTART:		// Software/System Restart
						case REASON_EXT_SYS_RST:		// External System Reset
						break;

						case REASON_WDT_RST:			// Hardware Watchdog
						case REASON_SOFT_WDT_RST:		// Software Watchdog
						case REASON_EXCEPTION_RST:		// Exception
						{
							if (resetInfo->reason == REASON_EXCEPTION_RST) {
								os_printf("Fatal exception (%d):\n", resetInfo->exccause);
							}

							// The address of the last crash is printed, which is used to debug garbled output.
							// os_printf("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x\n",
							// 	resetInfo->epc1, resetInfo->epc2, resetInfo->epc3, resetInfo->excvaddr, resetInfo->depc);
						}
						break;
					}
				}

				if (settingsError && settingsError != 0xff)
				{
					switch (settingsError)
					{
						case SETTINGS_ERROR_NO_DATA:        Request_SendMessage_ToPU("\x02" "ESP Restored",  0, 0, ID_MemoryIcon_); break;
						case SETTINGS_ERROR_CORRUPTED:      Request_SendMessage_ToPU("\x02" "ESP Corrupted", 0, 0, ID_MemoryIcon_); break;
						case SETTINGS_ERROR_USER_REQUEST:   Request_SendMessage_ToPU("\x02" "ESP RestByUsr", 0, 0, ID_MemoryIcon_); break;
					}

					// SendCommand(ESP_Prefix_Request, ESP_Suffix_RestoreBackup, settingsError);
					// NOTE: ESP_Suffix_RestoreBackup is buggy on PU850 side, keeping commented out

					settingsError = 0xff;
				}
			}
		}

		SerialPortReceiveProcess();

		if (!AsyncOTA.rebootRequired() && !Update.isRunning() && updateProgress == 0xff)
		{
			if (!mndpRunning) setupMNDP();

			MDNS.update();

			SSDP_Service();
		}

		Telnet_Service();

		// UdpService();

		return;
	}

	if (AsyncOTA.rebootRequired())
	{
		ws.textAll("rebooting");

		telnet.disconnectClient();

		WaitForSerialProcessing();

		SendESPStatus_ToPU(Booting_, true);

		U32 previousMillis = millis();

		while ((U32)(millis() - previousMillis) < validTimeOut_)
		{
			yield();
			SerialPortReceiveProcess();
			ESP.wdtFeed();
			delay(1);
		}

		ESP.restart();

		return;
	}

	Telnet_Service();

	ws.cleanupClients();

	// if (udpRunning) UdpService();

	#ifdef ShellOnSerial
	while (Serial.available() > 0) ShellService(Serial.read());
	#endif

	/*
	if (Serial.available())
	{
		while (Serial.available() && bufLen < sizeof(serialBuf) - 1)
		{
			serialBuf[bufLen++] = Serial.read();
			if ( !Serial.available() && bufLen < sizeof(serialBuf) - 1 ) delay(1);
		}
		serialBuf[bufLen] = 0;
		ws.binaryAll(serialBuf, bufLen);
		bufLen = 0;
	}
	*/

}
