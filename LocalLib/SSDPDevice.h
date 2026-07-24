#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "../ProductConfig.h"

#include "lwip/igmp.h"

#define SSDP_INTERVAL_SECONDS      FW_UPNP_CACHE_MAX_AGE_SECONDS
#define SSDP_PORT                  1900
#define SSDP_HTTP_PORT             80
#define SSDP_BUFFER_SIZE           64
#define SSDP_MULTICAST_TTL         FW_UPNP_MULTICAST_TTL
#define SSDP_QUEUE_SIZE            32

static const IPAddress SSDP_MULTICAST_ADDR(239, 255, 255, 250);

#define SSDP_UUID_SIZE             37
#define SSDP_SCHEMA_URL_SIZE       64
#define SSDP_DEVICE_TYPE_SIZE      64
#define SSDP_FRIENDLY_NAME_SIZE    64
#define SSDP_SERIAL_NUMBER_SIZE    32
#define SSDP_PRESENTATION_URL_SIZE 128
#define SSDP_MODEL_NAME_SIZE       64
#define SSDP_MODEL_URL_SIZE        128
#define SSDP_MODEL_VERSION_SIZE    32
#define SSDP_MANUFACTURER_SIZE     64
#define SSDP_MANUFACTURER_URL_SIZE 128

static const char* PROGMEM SSDP_RESPONSE_TEMPLATE =
	"HTTP/1.1 200 OK\r\n"
	"EXT:\r\n";

static const char* PROGMEM SSDP_NOTIFY_ALIVE_TEMPLATE =
	"NOTIFY * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"NTS: ssdp:alive\r\n";

static const char* PROGMEM SSDP_NOTIFY_UPDATE_TEMPLATE =
	"NOTIFY * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"NTS: ssdp:update\r\n";

static const char* PROGMEM SSDP_PACKET_TEMPLATE =
	"%s" // SSDP_RESPONSE_TEMPLATE / SSDP_NOTIFY_TEMPLATE
	"CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL_SECONDS
	"LOCATION: http://%s:%u/%s\r\n" // active interface IP, m_port, m_schemaURL
	"SERVER: ESP8266/1.0 UPnP/1.1 " FW_UPNP_SERVER_PRODUCT "/%s\r\n"
	"USN: %s%s%s\r\n" // m_uuid
	"%s: %s\r\n"  // "NT" or "ST", m_deviceType
	"BOOTID.UPNP.ORG: %u\r\n"
	"CONFIGID.UPNP.ORG: %u\r\n"
	"%s" // NEXTBOOTID.UPNP.ORG for ssdp:update
	"\r\n";

static const char* PROGMEM SSDP_NOTIFY_BYEBYE_PACKET_TEMPLATE =
	"NOTIFY * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"NT: %s\r\n"
	"NTS: ssdp:byebye\r\n"
	"USN: %s%s%s\r\n"
	"BOOTID.UPNP.ORG: %u\r\n"
	"CONFIGID.UPNP.ORG: %u\r\n"
	"\r\n";

enum ssdp_message_t : uint8_t {
	NOTIFY_ALIVE_INIT,
	NOTIFY_ALIVE,
	NOTIFY_UPDATE,
	NOTIFY_BYEBYE,
	RESPONSE
};

enum ssdp_udn_t : uint8_t {
	ROOT_FOR_ALL,
	ROOT_BY_UUID,
	ROOT_BY_TYPE,
	SERVICE_BY_TYPE
};

typedef struct {
	unsigned long time;

	ssdp_message_t type;
	ssdp_udn_t udn;
	IPAddress address;
	IPAddress interfaceAddress;
	uint16_t port;
	uint32_t bootId;
	uint32_t nextBootId;
} ssdp_send_parameters_t;

class SSDPDeviceClass {
public:
	SSDPDeviceClass();
	~SSDPDeviceClass();
	bool begin();
	void end();

	void schema(WiFiClient &client) const { schema(static_cast<Print &>(client)); }
	void schema(Print &print) const;

	void update();
	void handleClient();

	void setDeviceType(const String& deviceType) { setDeviceType(deviceType.c_str()); }
	void setDeviceType(const char *deviceType);

	void setUUID(const String& uuid)	{ setUUID(uuid.c_str()); }
	void setUUID(const char *uuid);

	void setName(const String& name) { setName(name.c_str()); }
	void setName(const char *name);
	void setURL(const String& url) { setURL(url.c_str()); }
	void setURL(const char *url);
	void setSchemaURL(const String& url) { setSchemaURL(url.c_str()); }
	void setSchemaURL(const char *url);
	void setSerialNumber(const String& serialNumber) { setSerialNumber(serialNumber.c_str()); }
	void setSerialNumber(const char *serialNumber);
	void setSerialNumber(const uint32_t serialNumber);
	void setModelName(const String& name) { setModelName(name.c_str()); }
	void setModelName(const char *name);
	void setModelNumber(const String& num) { setModelNumber(num.c_str()); }
	void setModelNumber(const char *num);
	void setModelURL(const String& url) { setModelURL(url.c_str()); }
	void setModelURL(const char *url);
	void setManufacturer(const String& name) { setManufacturer(name.c_str()); }
	void setManufacturer(const char *name);
	void setManufacturerURL(const String& url) { setManufacturerURL(url.c_str()); }
	void setManufacturerURL(const char *url);
	void setHTTPPort(uint16_t port);
	void setTTL(uint8_t ttl);
	void setInterval(uint32_t interval);

	const char *uuid() const { return m_uuid; }
	IPAddress activeInterfaceIP() const;

private:
	bool readLine(String &value);
	bool readKeyValue(String &key, String &value);

	void postNotifyALive(IPAddress interfaceAddress);
	void postNotifyUpdate(IPAddress interfaceAddress);
	void postResponse(long mx);
	void postResponse(ssdp_udn_t udn, long mx);
	void post(
		ssdp_message_t type,
		ssdp_udn_t udn,
		IPAddress address,
		IPAddress interfaceAddress,
		uint16_t port,
		unsigned long time
	);
	void clearQueue();
	void leaveJoinedInterfaces();
	void sendAllByebye(IPAddress interfaceAddress);
	void sendNotifyByebye(ssdp_udn_t udn, IPAddress interfaceAddress);
	bool serviceEnabled() const;
	IPAddress interfaceForRemote(IPAddress remote) const;

	void send(ssdp_send_parameters_t *parameters);

protected:
	WiFiUDP *m_server = nullptr;
	uint16_t m_port = SSDP_HTTP_PORT;
	uint8_t m_ttl = SSDP_MULTICAST_TTL;

	IPAddress m_last;
	IPAddress m_stationJoined;
	IPAddress m_accessPointJoined;
	uint32_t m_joinRetryAt = 0;
	uint32_t m_bootId = 1;
	uint32_t m_configId = 1;

	uint32_t m_interval = SSDP_INTERVAL_SECONDS;

	char m_schemaURL[SSDP_SCHEMA_URL_SIZE];
	char m_uuid[SSDP_UUID_SIZE];
	char m_deviceType[SSDP_DEVICE_TYPE_SIZE];
	char m_friendlyName[SSDP_FRIENDLY_NAME_SIZE];
	char m_serialNumber[SSDP_SERIAL_NUMBER_SIZE];
	char m_presentationURL[SSDP_PRESENTATION_URL_SIZE];
	char m_manufacturer[SSDP_MANUFACTURER_SIZE];
	char m_manufacturerURL[SSDP_MANUFACTURER_URL_SIZE];
	char m_modelName[SSDP_MODEL_NAME_SIZE];
	char m_modelURL[SSDP_MODEL_URL_SIZE];
	char m_modelNumber[SSDP_MODEL_VERSION_SIZE];

	ssdp_send_parameters_t m_queue[SSDP_QUEUE_SIZE];
};

extern SSDPDeviceClass SSDPDevice;
