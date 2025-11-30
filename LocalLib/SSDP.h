#pragma once

#include "Arduino.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "SSDPDevice.h"

#define SSDP SSDPDevice

/*
const char* SSDP_Name = "mySSDPName";
const char* modelName = "myDevice";
const char* nVersion = "v1.11";
const char* SerialNumber = "000000001";
const char* Manufacturer = "Company";
const char* ManufacturerURL = "https://example.com";

byte mac[6];
char base[UUIDBASE_SIZE];
WiFi.macAddress(mac);
device.begin((const char*)base, mac);
device.serialNumber((char*)"A0123456789");
device.manufacturer((char*)"Espressif");
device.manufacturerURL((char*)"http://espressif.com");
device.modelName((char*)"ESP-12e");
device.modelNumber(1, 0);
device.friendlyName((char*)"ESP8266");
device.presentationURL((char*)"/");
SSDP.begin(&device);
*/

/*
#define SSDP_INTERVAL	 1200
#define SSDP_PORT	 1900

#define METHOD_SIZE		 10
#define URI_SIZE		 2
#define BUFFER_SIZE		 48

#define UUIDBASE_SIZE			24
#define PRESENTATIONURL_SIZE		32
#define FRIENDLYNAME_SIZE		32
#define MODELNAME_SIZE			32
#define SERIALNUMBER_SIZE		32
#define MANUFACTURER_SIZE		32
#define MANUFACTURERURL_SIZE		32

typedef enum { BASIC, MANAGEABLE } device_t;

typedef struct {
	uint8_t major;
	uint8_t minor;
} version_t;

class SSDPDevice {
	typedef enum {NONE, SEARCH, NOTIFY} method_t;

public:
	SSDPDevice();
	~SSDPDevice();

	void begin(SSDPDevice *device);
	void begin(const char *base, byte *mac, device_t deviceType);
	void begin(const char *base, byte *mac){
		begin(base, mac, BASIC);
	}

	uint8_t process();
	void send(method_t method);
	void schema(WiFiClient *client);

	char *uuid();

	device_t deviceType();
	void deviceType(device_t deviceType);

	char *presentationURL();
	void presentationURL(char *presentationURL);

	char *friendlyName();
	void friendlyName(char *friendlyName);

	char *modelName();
	void modelName(char *modelName);

	version_t *modelNumber();
	void modelNumber(uint8_t major, uint8_t minor);

	char *serialNumber();
	void serialNumber(char *serialNumber);

	char *manufacturer();
	void manufacturer(char *manufacturer);

	char *manufacturerURL();
	void manufacturerURL(char *manufacturerURL);

private:
	WiFiUDP _server;

	SSDPDevice *_device;

	bool _pending;
	unsigned short _delay;
	unsigned long _process_time;
	unsigned long _notify_time;

	byte *_mac;
	char *_base;

	device_t _deviceType;
	version_t *_modelNumber;

	char *_presentationURL;
	char *_friendlyName;
	char *_manufacturer;
	char *_manufacturerURL;
	char *_modelName;
	char *_serialNumber;
};
*/

/*
SSDPDevice::SSDPDevice():
	_base(0),
	_mac(0),
	_presentationURL(0),
	_friendlyName(0),
	_modelName(0),
	_modelNumber(0),
	_serialNumber(0),
	_manufacturer(0),
	_manufacturerURL(0)
{
	_deviceType = BASIC;
}

void SSDPDevice::begin(const char *base, byte *mac, device_t deviceType) {
	_base = (char*)os_malloc(UUIDBASE_SIZE);
	_mac = (byte*)os_malloc(6);
	_presentationURL = (char*)os_malloc(PRESENTATIONURL_SIZE);
	_friendlyName = (char*)os_malloc(FRIENDLYNAME_SIZE);
	_modelName = (char*)os_malloc(MODELNAME_SIZE);
	_modelNumber = (version_t*)os_malloc(2);
	_serialNumber = (char*)os_malloc(SERIALNUMBER_SIZE);
	_manufacturer = (char*)os_malloc(MANUFACTURER_SIZE);
	_manufacturerURL = (char*)os_malloc(MANUFACTURERURL_SIZE);

	_deviceType = deviceType;
	_modelNumber->major = 0;
	_modelNumber->minor = 0;
	memcpy(_mac, mac, 6);
	strcpy(_base, base);
}

SSDPDevice::~SSDPDevice() {
	os_free(_base);
	os_free(_mac);
	os_free(_presentationURL);
	os_free(_friendlyName);
	os_free(_modelName);
	os_free(_modelNumber);
	os_free(_serialNumber);
	os_free(_manufacturer);
	os_free(_manufacturerURL);

	delete _device;
}

SSDPDevice::SSDPDevice(): _device(0) {
	_pending = false;
}

void SSDPDevice::begin(SSDPDevice *device) {
	_device = device;
	_pending = false;

	struct ip_info staIpInfo;
	wifi_get_ip_info(STATION_IF, &staIpInfo);
	ip_addr_t ifaddr;
	ifaddr.addr = staIpInfo.ip.addr;
	ip_addr_t multicast_addr;
	multicast_addr.addr = (uint32_t) SSDP_MULTICAST_ADDR;
	igmp_joingroup(&ifaddr, &multicast_addr);

	_server.begin(SSDP_PORT);
}

uint8_t SSDPDevice::process() {
	if(!_pending && _server.parsePacket() > 0){
		method_t method = NONE;

		typedef enum {METHOD, URI, PROTO, KEY, VALUE, ABORT} states;
		states state = METHOD;

		typedef enum {START, MAN, ST, MX} headers;
		headers header = START;

		uint8_t cursor = 0;
		uint8_t cr = 0;

		char buffer[BUFFER_SIZE] = {0};

		while(_server.available() > 0){
			char c = _server.read();

			(c == '\r' || c == '\n') ? cr++ : cr = 0;

			switch(state){
				case METHOD:
					if(c == ' '){
						if(strcmp(buffer, "M-SEARCH") == 0) method = SEARCH;
						else if(strcmp(buffer, "NOTIFY") == 0) method = NOTIFY;

						if(method == NONE) state = ABORT;
						else state = URI;
						cursor = 0;

					}else if(cursor < METHOD_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
					break;
				case URI:
					if(c == ' '){
						if(strcmp(buffer, "*")) state = ABORT;
						else state = PROTO;
						cursor = 0;
					}else if(cursor < URI_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
					break;
				case PROTO:
					if(cr == 2){ state = KEY; cursor = 0; }
					break;
				case KEY:
					if(cr == 4){ _pending = true; _process_time = millis(); }
					else if(c == ' '){ cursor = 0; state = VALUE; }
					else if(c != '\r' && c != '\n' && c != ':' && cursor < BUFFER_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
					break;
				case VALUE:
					if(cr == 2){
						switch(header){
							case MAN:
								// strncpy(_head.man, buffer, HEAD_VAL_SIZE);
								break;
							case ST:
								if(strcmp(buffer, "ssdp:all")){
									state = ABORT;
									#if DEBUG > 0
									Serial.print("REJECT: ");
									Serial.println(buffer);
									#endif
								}
								break;
							case MX:
								_delay = random(0, atoi(buffer)) * 1000L;
								break;
						}

						if(state != ABORT){ state = KEY; header = START; cursor = 0; }
					}else if(c != '\r' && c != '\n'){
						if(header == START){
							if(strncmp(buffer, "MA", 2) == 0) header = MAN;
							else if(strcmp(buffer, "ST") == 0) header = ST;
							else if(strcmp(buffer, "MX") == 0) header = MX;
						}

						if(cursor < BUFFER_SIZE - 1){ buffer[cursor++] = c; buffer[cursor] = '\0'; }
					}
					break;
				case ABORT:
					_pending = false; _delay = 0;
					break;
			}
		}

		_server.flush();
	}

	if(_pending && (millis() - _process_time) > _delay){
		_pending = false; _delay = 0;
		send(NONE);
	}else if(_notify_time == 0 || (millis() - _notify_time) > (SSDP_INTERVAL * 1000L)){
		_notify_time = millis();
		send(NOTIFY);
	}
}

void SSDPDevice::send(method_t method){
	version_t *modelNumber = _device->modelNumber();
	byte ssdp[4] = {239, 255, 255, 250};

	if(method == NONE){
		#if DEBUG > 0
		Serial.print("Sending Response to ");
		Serial.print(_server.remoteIP());
		Serial.print(":");
		Serial.println(_server.remotePort());
		#endif

		_server.beginPacket(_server.remoteIP(), _server.remotePort());
		_server.println("HTTP/1.1 200 OK");
		_server.println("EXT:");
		_server.println("ST: upnp:rootdevice");
	}else if(method == NOTIFY){
		#if DEBUG > 0
		Serial.println("Sending Notify to 239.255.255.250:1900");
		#endif

		_server.beginPacket(ssdp, SSDP_PORT);
		_server.println("NOTIFY * HTTP/1.1");
		_server.println("HOST: 239.255.255.250:1900");
		_server.println("NT: upnp:rootdevice");
		_server.println("NTS: ssdp:alive");
	}

	_server.print("CACHE-CONTROL: max-age=");
	_server.println(SSDP_INTERVAL);

	_server.print("SERVER: UPNP/1.1 ");
	_server.print(_device->modelName());
	if(modelNumber->major > 0 || modelNumber->minor > 0){
		_server.print("/");
		_server.print(modelNumber->major);
		_server.print(".");
		_server.print(modelNumber->minor);
	}
	_server.println();

	_server.print("USN: uuid:");
	_server.println(_device->uuid());

	_server.print("LOCATION: http://");
	_server.print(WiFi.localIP());
	_server.println("/ssdp/schema.xml");
	_server.println();

	_server.endPacket();
}

void SSDPDevice::schema(WiFiClient *client){
	client->println("HTTP/1.1 200 OK");
	client->println("Content-Type: text/xml");
	client->println();

	client->println("<?xml version=\"1.0\"?>");
	client->println("<root xmlns=\"urn:schemas-upnp-org:device-1-0\">");
	client->println("\t<specVersion>");
	client->println("\t\t<major>1</major>");
	client->println("\t\t<minor>0</minor>");
	client->println("\t</specVersion>");

	client->println("\t<device>");
	client->println("\t\t<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>");

	if(strlen(_device->presentationURL())){
		client->print("\t<presentationURL>");
		client->print(_device->presentationURL());
		client->print("</presentationURL>\r\n");
	}

	client->print("\t\t<friendlyName>");
	client->print(_device->friendlyName());
	client->print("</friendlyName>\r\n");

	client->print("\t\t<modelName>");
	client->print(_device->modelName());
	client->print("</modelName>\r\n");

	version_t *modelNumber = _device->modelNumber();

	if(modelNumber->major > 0 || modelNumber->minor > 0){
		client->print("\t\t<modelNumber>");
		client->print(modelNumber->major);
		client->print(".");
		client->print(modelNumber->minor);
		client->print("</modelNumber>\r\n");
	}

	if(strlen(_device->serialNumber())){
		client->print("\t\t<serialNumber>");
		client->print(_device->serialNumber());
		client->print("</serialNumber>\r\n");
	}

	client->print("\t\t<manufacturer>");
	client->print(_device->manufacturer());
	client->print("</manufacturer>\r\n");

	if(strlen(_device->manufacturerURL())){
		client->print("\t\t<manufacturerURL>");
		client->print(_device->manufacturerURL());
		client->print("</manufacturerURL>\r\n");
	}

	client->print("\t\t<UDN>uuid:");
	client->print(_device->uuid());
	client->print("</UDN>\r\n");

	client->println("\t</device>");
	client->println("</root>");
}

char *SSDPDevice::uuid(){
	char *_uuid = (char*)os_malloc(37);
	sprintf(_uuid, "%s-%02X%02X%02X%02X%02X%02X", _base, _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
	return _uuid;
}

device_t SSDPDevice::deviceType(){
	return _deviceType;
}

void SSDPDevice::deviceType(device_t deviceType){
	_deviceType = deviceType;
}

char *SSDPDevice::presentationURL(){
	return _presentationURL;
}

void SSDPDevice::presentationURL(char *presentationURL){
	strcpy(_presentationURL, presentationURL);
}

char *SSDPDevice::friendlyName(){
	return _friendlyName;
}

void SSDPDevice::friendlyName(char *friendlyName){
	strcpy(_friendlyName, friendlyName);
}

char *SSDPDevice::modelName(){
	return _modelName;
}

void SSDPDevice::modelName(char *modelName){
	strcpy(_modelName, modelName);
}

version_t *SSDPDevice::modelNumber(){
	return _modelNumber;
}

void SSDPDevice::modelNumber(uint8_t major, uint8_t minor){
	_modelNumber->major = major;
	_modelNumber->minor = minor;
}

char *SSDPDevice::serialNumber(){
	return _serialNumber;
}

void SSDPDevice::serialNumber(char *serialNumber){
	strcpy(_serialNumber, serialNumber);
}

char *SSDPDevice::manufacturer(){
	return _manufacturer;
}

void SSDPDevice::manufacturer(char *manufacturer){
	strcpy(_manufacturer, manufacturer);
}

char *SSDPDevice::manufacturerURL(){
	return _manufacturerURL;
}

void SSDPDevice::manufacturerURL(char *manufacturerURL){
	strcpy(_manufacturerURL, manufacturerURL);
}
*/

/*

bool SSDPDeviceClass::begin() {
	end();

	_pending = false;
	_st_is_uuid = false;

	assert(NULL == m_server);

	m_server = new UdpContext;
	m_server->ref();

	IPAddress local = WiFi.localIP();
	IPAddress mcast(SSDP_MULTICAST_ADDR);

	if (igmp_joingroup(local, mcast) != ERR_OK ) {
		return false;
	}

	if (!m_server->listen(IP_ADDR_ANY, SSDP_PORT)) {
		return false;
	}

	m_server->setMulticastInterface(local);
	m_server->setMulticastTTL(_ttl);
	m_server->onRx(std::bind(&SSDPDeviceClass::update, this));

	if (!m_server->connect(mcast, SSDP_PORT)) {
		return false;
	}

	_startTimer();

	return true;
}

void SSDPDeviceClass::end() {
	if (!m_server)
		return;

	_stopTimer();

	m_server->disconnect();

	IPAddress local = WiFi.localIP();
	IPAddress mcast(SSDP_MULTICAST_ADDR);

	if (igmp_leavegroup(local, mcast) != ERR_OK ) {
	}

	m_server->unref();
	m_server = 0;
}

void SSDPDeviceClass::send(ssdp_method_t method) {
	char buffer[1460];
	IPAddress ip = WiFi.localIP();

	char valueBuffer[strlen_P(SSDP_NOTIFY_TEMPLATE) + 1];
	strcpy_P(valueBuffer, (method == NONE) ? SSDP_RESPONSE_TEMPLATE : SSDP_NOTIFY_TEMPLATE);

	int len = snprintf_P(buffer, sizeof(buffer),
		SSDP_PACKET_TEMPLATE,
		valueBuffer,
		m_interval,
		m_modelName,
		m_modelNumber,
		m_uuid,
		(method == NONE) ? "ST" : "NT",
		(_st_is_uuid) ? m_uuid : m_deviceType,
		ip.toString().c_str(), m_port, m_schemaURL
	);

	m_server->append(buffer, len);

	IPAddress remoteAddr;
	uint16_t remotePort;
	if (method == NONE) {
		remoteAddr = _respondToAddr;
		remotePort = _respondToPort;
	} else {
		remoteAddr = IPAddress(SSDP_MULTICAST_ADDR);
		remotePort = SSDP_PORT;
	}

	m_server->send(remoteAddr, remotePort);
}

void SSDPDeviceClass::update() {
	if (!_pending && m_server->next()) {
		ssdp_method_t method = NONE;

		_respondToAddr = m_server->getRemoteAddress();
		_respondToPort = m_server->getRemotePort();

		typedef enum {METHOD, URI, PROTO, KEY, VALUE, ABORT} states;
		states state = METHOD;

		typedef enum {START, MAN, ST, MX} headers;
		headers header = START;

		uint8_t cursor = 0;
		uint8_t cr = 0;

		char buffer[SSDP_BUFFER_SIZE] = {0};

		while (m_server->getSize() > 0) {
			char c = m_server->read();

			(c == '\r' || c == '\n') ? cr++ : cr = 0;

			switch (state) {
				case METHOD:
					if (c == ' ') {
						if (strcmp(buffer, "M-SEARCH") == 0) method = SEARCH;

						if (method == NONE) state = ABORT;
						else state = URI;
						cursor = 0;

					} else if (cursor < SSDP_METHOD_SIZE - 1) {
						buffer[cursor++] = c;
						buffer[cursor] = '\0';
					}
					break;
				case URI:
					if (c == ' ') {
						if (strcmp(buffer, "*")) state = ABORT;
						else state = PROTO;
						cursor = 0;
					} else if (cursor < SSDP_URI_SIZE - 1) {
						buffer[cursor++] = c;
						buffer[cursor] = '\0';
					}
					break;
				case PROTO:
					if (cr == 2) {
						state = KEY;
						cursor = 0;
					}
					break;
				case KEY:
					if (cr == 4) {
						_pending = true;
						_process_time = millis();
					}
					else if (c == ' ') {
						cursor = 0;
						state = VALUE;
					}
					else if (c != '\r' && c != '\n' && c != ':' && cursor < SSDP_BUFFER_SIZE - 1) {
						buffer[cursor++] = c;
						buffer[cursor] = '\0';
					}
					break;
				case VALUE:
					if (cr == 2) {
						switch (header) {
							case START:
								break;
							case MAN:
								#ifdef DEBUG_SSDP
								DEBUG_SSDP.printf("MAN: %s\n", (char *)buffer);
								#endif
								break;
							case ST:
								if (strcmp(buffer, "ssdp:all")) {
									state = ABORT;
									#ifdef DEBUG_SSDP
									DEBUG_SSDP.printf("REJECT: %s\n", (char *)buffer);
									#endif
								} else {
									_st_is_uuid = false;
								}
								// if the search type matches our type, we should respond instead of ABORT
								if (strcasecmp(buffer, m_deviceType) == 0) {
									_pending = true;
									_st_is_uuid = false;
									_process_time = millis();
									state = KEY;
								}
								if (strcasecmp(buffer, m_uuid) == 0) {
									_pending = true;
									_st_is_uuid = true;
									_process_time = millis();
									state = KEY;
								}
								break;
							case MX:
								_delay = random(0, atoi(buffer)) * 1000L;
								break;
						}

						if (state != ABORT) {
							state = KEY;
							header = START;
							cursor = 0;
						}
					} else if (c != '\r' && c != '\n') {
						if (header == START) {
							if (strncmp(buffer, "MA", 2) == 0) header = MAN;
							else if (strcmp(buffer, "ST") == 0) header = ST;
							else if (strcmp(buffer, "MX") == 0) header = MX;
						}

						if (cursor < SSDP_BUFFER_SIZE - 1) {
							buffer[cursor++] = c;
							buffer[cursor] = '\0';
						}
					}
					break;
				case ABORT:
					_pending = false; _delay = 0;
					break;
			}
		}
	}

	if (_pending && (millis() - _process_time) > _delay) {
		_pending = false; _delay = 0;
		send(NONE);
	} else if(_notify_time == 0 || (millis() - _notify_time) > (m_interval * 1000L)){
		_notify_time = millis();
		_st_is_uuid = false;
		send(NOTIFY);
	}

	if (_pending) {
		while (m_server->next())
			m_server->flush();
	}

}
*/
