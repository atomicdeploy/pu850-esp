#include "SSDPDevice.h"

#include <new>

SSDPDeviceClass::SSDPDeviceClass() :
	m_server(0),
	m_port(SSDP_HTTP_PORT),
	m_ttl(SSDP_MULTICAST_TTL),
	m_last(0, 0, 0, 0),
	m_stationJoined(0, 0, 0, 0),
	m_accessPointJoined(0, 0, 0, 0)
{
	m_uuid[0] = '\0';
	m_modelNumber[0] = '\0';
	strlcpy(m_deviceType, FW_UPNP_DEVICE_TYPE, sizeof(m_deviceType));
	m_friendlyName[0] = '\0';
	m_presentationURL[0] = '\0';
	m_serialNumber[0] = '\0';
	m_modelName[0] = '\0';
	m_modelURL[0] = '\0';
	m_manufacturer[0] = '\0';
	m_manufacturerURL[0] = '\0';
	strlcpy(m_schemaURL, FW_UPNP_SCHEMA_PATH, sizeof(m_schemaURL));

	uint32_t chipId = ESP.getChipId();

	sprintf_P(m_uuid, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"),
		(uint16_t) ((chipId >> 16) & 0xff),
		(uint16_t) ((chipId >>  8) & 0xff),
		(uint16_t)   chipId        & 0xff);

	// CONFIGID identifies the description shape, not a particular boot. Keep it
	// deterministic and inside the UPnP-defined 24-bit range.
	uint32_t configHash = 2166136261UL;
	const char *identity[] = {
		FW_UPNP_DEVICE_TYPE,
		FW_UPNP_SERVICE_TYPE,
		FW_PRODUCT_MODEL_NAME,
		FW_MANUFACTURER_NAME
	};
	for (const char *value : identity) {
		while (*value) {
			configHash ^= static_cast<uint8_t>(*value++);
			configHash *= 16777619UL;
		}
	}
	m_configId = configHash & 0x00ffffffUL;
	if (m_configId == 0) m_configId = 1;

	clearQueue();
}

SSDPDeviceClass::~SSDPDeviceClass() {
	end();
}

bool SSDPDeviceClass::begin() {
	end();

	m_bootId = ESP.getCycleCount() & 0x7fffffffUL;
	if (m_bootId == 0) m_bootId = 1;
	clearQueue();

	return true;
}

void SSDPDeviceClass::end() {
	if (m_server) {
		sendAllByebye(m_stationJoined);
		if (m_accessPointJoined != m_stationJoined) {
			sendAllByebye(m_accessPointJoined);
		}

		m_server->stop();
		leaveJoinedInterfaces();
		delete m_server;
		m_server = nullptr;
	}

	m_last = IPAddress(0, 0, 0, 0);
	m_stationJoined = IPAddress(0, 0, 0, 0);
	m_accessPointJoined = IPAddress(0, 0, 0, 0);
	m_joinRetryAt = 0;

	clearQueue();
}

void SSDPDeviceClass::update() {
	if (!m_server || m_last == IPAddress(0, 0, 0, 0)) return;

	// Collapse overlapping configuration-change requests into the update that
	// is already queued so BOOTID/NEXTBOOTID remain a contiguous pair.
	for (uint8_t index = 0; index < SSDP_QUEUE_SIZE; ++index) {
		if (m_queue[index].time != 0 && m_queue[index].type == NOTIFY_UPDATE) {
			return;
		}
	}

	const uint32_t nextBootId = m_bootId == 0x7fffffffUL ? 1UL : m_bootId + 1UL;
	postNotifyUpdate(m_stationJoined);
	if (m_accessPointJoined != m_stationJoined) {
		postNotifyUpdate(m_accessPointJoined);
	}

	// postNotifyUpdate captures the old and next IDs in each queue entry.
	m_bootId = nextBootId;
}

bool SSDPDeviceClass::readLine(String &value) {
	char buffer[65];
	int bufferPos = 0;

	while (1) {
		int c = m_server->read();

		if (c < 0) {
			buffer[bufferPos] = '\0';

			break;
		}
		if (c == '\r' && m_server->peek() == '\n') {
			m_server->read();

			buffer[bufferPos] = '\0';

			break;
		}
		if (bufferPos < 64) {
			buffer[bufferPos++] = c;
		}
	}

	value = String(buffer);

	return bufferPos > 0;
}

bool SSDPDeviceClass::readKeyValue(String &key, String &value) {
	char buffer[65];
	int bufferPos = 0;

	while (1) {
		int c = m_server->read();

		if (c < 0) {
			if (bufferPos == 0) return false;

			buffer[bufferPos] = '\0';

			break;
		}
		if (c == ':') {
			buffer[bufferPos] = '\0';

			while (m_server->peek() == ' ') m_server->read();

			break;
		}
		else if (c == '\r' && m_server->peek() == '\n') {
			m_server->read();

			if (bufferPos == 0) return false;

			buffer[bufferPos] = '\0';

			key = String();
			value = String(buffer);

			return true;
		}
		if (bufferPos < 64) {
			buffer[bufferPos++] = c;
		}
	}

	key = String(buffer);

	readLine(value);

	return true;
}

bool SSDPDeviceClass::serviceEnabled() const {
	return FW_UPNP_SERVICE_TYPE[0] != '\0';
}

void SSDPDeviceClass::postNotifyALive(IPAddress interfaceAddress) {
	if (interfaceAddress == IPAddress(0, 0, 0, 0)) return;

	unsigned long time = millis();

	post(NOTIFY_ALIVE_INIT, ROOT_FOR_ALL, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 10);
	post(NOTIFY_ALIVE_INIT, ROOT_BY_UUID, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 30);
	post(NOTIFY_ALIVE_INIT, ROOT_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 50);
	if (serviceEnabled()) {
		post(NOTIFY_ALIVE_INIT, SERVICE_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 70);
	}

	post(NOTIFY_ALIVE_INIT, ROOT_FOR_ALL, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 210);
	post(NOTIFY_ALIVE_INIT, ROOT_BY_UUID, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 230);
	post(NOTIFY_ALIVE_INIT, ROOT_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 250);
	if (serviceEnabled()) {
		post(NOTIFY_ALIVE_INIT, SERVICE_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 270);
	}

	post(NOTIFY_ALIVE, ROOT_FOR_ALL, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 610);
	post(NOTIFY_ALIVE, ROOT_BY_UUID, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 630);
	post(NOTIFY_ALIVE, ROOT_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 650);
	if (serviceEnabled()) {
		post(NOTIFY_ALIVE, SERVICE_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 670);
	}
}

void SSDPDeviceClass::postNotifyUpdate(IPAddress interfaceAddress) {
	if (interfaceAddress == IPAddress(0, 0, 0, 0)) return;

	unsigned long time = millis();

	post(NOTIFY_UPDATE, ROOT_FOR_ALL, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 10);
	post(NOTIFY_UPDATE, ROOT_BY_UUID, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 30);
	post(NOTIFY_UPDATE, ROOT_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 50);
	if (serviceEnabled()) {
		post(NOTIFY_UPDATE, SERVICE_BY_TYPE, SSDP_MULTICAST_ADDR, interfaceAddress, SSDP_PORT, time + 70);
	}
}

void SSDPDeviceClass::postResponse(long mx) {
	unsigned long time = millis();
	const unsigned long maxDelay = static_cast<unsigned long>(max(1L, mx)) * 1000UL;

	IPAddress address = m_server->remoteIP();
	IPAddress interfaceAddress = interfaceForRemote(address);
	uint16_t port = m_server->remotePort();

	post(RESPONSE, ROOT_FOR_ALL, address, interfaceAddress, port, time + random(static_cast<long>(maxDelay)));
	post(RESPONSE, ROOT_BY_UUID, address, interfaceAddress, port, time + random(static_cast<long>(maxDelay)));
	post(RESPONSE, ROOT_BY_TYPE, address, interfaceAddress, port, time + random(static_cast<long>(maxDelay)));
	if (serviceEnabled()) {
		post(RESPONSE, SERVICE_BY_TYPE, address, interfaceAddress, port, time + random(static_cast<long>(maxDelay)));
	}
}

void SSDPDeviceClass::postResponse(ssdp_udn_t udn, long mx) {
	const unsigned long maxDelay = static_cast<unsigned long>(max(1L, mx)) * 1000UL;
	IPAddress address = m_server->remoteIP();
	post(
		RESPONSE,
		udn,
		address,
		interfaceForRemote(address),
		m_server->remotePort(),
		millis() + random(static_cast<long>(maxDelay))
	);
}

void SSDPDeviceClass::post(
	ssdp_message_t type,
	ssdp_udn_t udn,
	IPAddress address,
	IPAddress interfaceAddress,
	uint16_t port,
	unsigned long time
) {
	int8_t freeIndex = -1;
	int8_t disposableResponse = -1;

	for (uint8_t i = 0; i < SSDP_QUEUE_SIZE; i++) {
		if (
			type != NOTIFY_ALIVE_INIT &&
			m_queue[i].time != 0 &&
			m_queue[i].type == type &&
			m_queue[i].udn == udn &&
			m_queue[i].address == address &&
			m_queue[i].interfaceAddress == interfaceAddress &&
			m_queue[i].port == port
		) {
			// De-duplicate bursts from repeated M-SEARCH packets and repeated
			// lifecycle requests. Preserve the earliest legal send time.
			if (static_cast<int32_t>(m_queue[i].time - time) > 0) {
				m_queue[i].time = time;
			}
			m_queue[i].bootId = m_bootId;
			m_queue[i].nextBootId =
				type == NOTIFY_UPDATE
					? (m_bootId == 0x7fffffffUL ? 1UL : m_bootId + 1UL)
					: 0;
			return;
		}
		if (m_queue[i].time == 0 && freeIndex < 0) freeIndex = i;
		if (m_queue[i].time != 0 && m_queue[i].type == RESPONSE && disposableResponse < 0) {
			disposableResponse = i;
		}
	}

	int8_t index = freeIndex;
	if (index < 0 && type != RESPONSE) index = disposableResponse;
	if (index < 0) return;

	m_queue[index].type = type;
	m_queue[index].udn = udn;
	m_queue[index].address = address;
	m_queue[index].interfaceAddress = interfaceAddress;
	m_queue[index].port = port;
	m_queue[index].time = time;
	m_queue[index].bootId = m_bootId;
	m_queue[index].nextBootId =
		type == NOTIFY_UPDATE
			? (m_bootId == 0x7fffffffUL ? 1UL : m_bootId + 1UL)
			: 0;
}

void SSDPDeviceClass::send(ssdp_send_parameters_t *parameters) {
	char buffer[768];

	const char *typeTemplate;
	const char *uri, *usn1, *usn2, *usn3;

	switch (parameters->type) {
		case NOTIFY_ALIVE_INIT:
		case NOTIFY_ALIVE:
			typeTemplate = SSDP_NOTIFY_ALIVE_TEMPLATE;
			break;
		case NOTIFY_UPDATE:
			typeTemplate = SSDP_NOTIFY_UPDATE_TEMPLATE;
			break;
		case NOTIFY_BYEBYE:
			typeTemplate = nullptr;
			break;
		default: // RESPONSE
			typeTemplate = SSDP_RESPONSE_TEMPLATE;
			break;
	}

	String uuid = "uuid:" + String(m_uuid);

	switch (parameters->udn) {
		case ROOT_FOR_ALL:
			uri = "upnp:rootdevice";
			usn1 = uuid.c_str();
			usn2 = "::";
			usn3 = "upnp:rootdevice";
			break;
		case ROOT_BY_UUID:
			uri = uuid.c_str();
			usn1 = uuid.c_str();
			usn2 = "";
			usn3 = "";
			break;
		case ROOT_BY_TYPE:
			uri = m_deviceType;
			usn1 = uuid.c_str();
			usn2 = "::";
			usn3 = m_deviceType;
			break;
		case SERVICE_BY_TYPE:
			if (!serviceEnabled()) return;
			uri = FW_UPNP_SERVICE_TYPE;
			usn1 = uuid.c_str();
			usn2 = "::";
			usn3 = FW_UPNP_SERVICE_TYPE;
			break;
		default: return;
	}

	if (parameters->type == NOTIFY_BYEBYE) {
		int len = snprintf_P(
			buffer,
			sizeof(buffer),
			SSDP_NOTIFY_BYEBYE_PACKET_TEMPLATE,
			uri,
			usn1,
			usn2,
			usn3,
			parameters->bootId,
			m_configId
		);
		if (
			len > 0 &&
			static_cast<size_t>(len) < sizeof(buffer) &&
			m_server->beginPacketMulticast(
				SSDP_MULTICAST_ADDR,
				SSDP_PORT,
				parameters->interfaceAddress,
				m_ttl
			)
		) {
			m_server->write(reinterpret_cast<const uint8_t *>(buffer), static_cast<size_t>(len));
			m_server->endPacket();
		}

		parameters->time = 0;
		return;
	}

	IPAddress ip = parameters->interfaceAddress;
	if (ip == IPAddress(0, 0, 0, 0)) {
		parameters->time = 0;
		return;
	}

	if (parameters->type != NOTIFY_UPDATE) {
		parameters->bootId = m_bootId;
		parameters->nextBootId = 0;
	}

	char nextBootHeader[48] = { 0 };
	if (parameters->type == NOTIFY_UPDATE && parameters->nextBootId != 0) {
		snprintf(
			nextBootHeader,
			sizeof(nextBootHeader),
			"NEXTBOOTID.UPNP.ORG: %lu\r\n",
			static_cast<unsigned long>(parameters->nextBootId)
		);
	}

	const char *serverVersion =
		m_modelNumber[0] == '\0' ? FW_UPNP_SERVER_VERSION : m_modelNumber;
	char ipText[16];
	snprintf(
		ipText,
		sizeof(ipText),
		"%u.%u.%u.%u",
		ip[0],
		ip[1],
		ip[2],
		ip[3]
	);

	int len = snprintf_P(
		buffer,
		sizeof(buffer),
		SSDP_PACKET_TEMPLATE,
		typeTemplate,
		m_interval,
		ipText,
		m_port,
		m_schemaURL,
		serverVersion,
		usn1,
		usn2,
		usn3,
		parameters->type == RESPONSE ? "ST" : "NT",
		uri,
		parameters->bootId,
		m_configId,
		nextBootHeader
	);
	if (len <= 0 || static_cast<size_t>(len) >= sizeof(buffer)) {
		parameters->time = 0;
		return;
	}

	if (parameters->address == SSDP_MULTICAST_ADDR) {
		if (
			!m_server->beginPacketMulticast(
				parameters->address,
				parameters->port,
				parameters->interfaceAddress,
				m_ttl
			)
		) {
			parameters->time = 0;
			return;
		}
	}
	else {
		if (!m_server->beginPacket(parameters->address, parameters->port)) {
			parameters->time = 0;
			return;
		}
	}

	m_server->write(reinterpret_cast<const uint8_t *>(buffer), static_cast<size_t>(len));
	m_server->endPacket();

	// Refresh the complete advertisement set before half of max-age.
	parameters->time =
		parameters->type == NOTIFY_ALIVE
			? millis() + m_interval * 450UL
			: 0;
}

void SSDPDeviceClass::sendNotifyByebye(
	ssdp_udn_t udn,
	IPAddress interfaceAddress
) {
	if (!m_server || interfaceAddress == IPAddress(0, 0, 0, 0)) return;

	ssdp_send_parameters_t parameters = {};
	parameters.time = millis();
	parameters.type = NOTIFY_BYEBYE;
	parameters.udn = udn;
	parameters.address = SSDP_MULTICAST_ADDR;
	parameters.interfaceAddress = interfaceAddress;
	parameters.port = SSDP_PORT;
	parameters.bootId = m_bootId;
	send(&parameters);
}

void SSDPDeviceClass::sendAllByebye(IPAddress interfaceAddress) {
	if (interfaceAddress == IPAddress(0, 0, 0, 0)) return;

	sendNotifyByebye(ROOT_FOR_ALL, interfaceAddress);
	sendNotifyByebye(ROOT_BY_UUID, interfaceAddress);
	sendNotifyByebye(ROOT_BY_TYPE, interfaceAddress);
	if (serviceEnabled()) {
		sendNotifyByebye(SERVICE_BY_TYPE, interfaceAddress);
	}
}

void SSDPDeviceClass::clearQueue() {
	for (uint8_t index = 0; index < SSDP_QUEUE_SIZE; ++index) {
		m_queue[index].time = 0;
	}
}

void SSDPDeviceClass::leaveJoinedInterfaces() {
	if (m_stationJoined != IPAddress(0, 0, 0, 0)) {
		igmp_leavegroup(m_stationJoined, SSDP_MULTICAST_ADDR);
	}
	if (
		m_accessPointJoined != IPAddress(0, 0, 0, 0) &&
		m_accessPointJoined != m_stationJoined
	) {
		igmp_leavegroup(m_accessPointJoined, SSDP_MULTICAST_ADDR);
	}
}

IPAddress SSDPDeviceClass::interfaceForRemote(IPAddress remote) const {
	if (m_accessPointJoined != IPAddress(0, 0, 0, 0)) {
		// ESP8266 soft-AP defaults to a /24. If both interfaces are ever joined,
		// prefer it for peers on that subnet.
		const IPAddress mask(255, 255, 255, 0);
		bool sameSubnet = true;
		for (uint8_t index = 0; index < 4; ++index) {
			if (
				(remote[index] & mask[index]) !=
				(m_accessPointJoined[index] & mask[index])
			) {
				sameSubnet = false;
				break;
			}
		}
		if (sameSubnet) return m_accessPointJoined;
	}

	if (m_stationJoined != IPAddress(0, 0, 0, 0)) {
		const IPAddress mask = WiFi.subnetMask();
		bool sameSubnet = mask != IPAddress(0, 0, 0, 0);
		for (uint8_t index = 0; sameSubnet && index < 4; ++index) {
			if (
				(remote[index] & mask[index]) !=
				(m_stationJoined[index] & mask[index])
			) {
				sameSubnet = false;
			}
		}
		if (sameSubnet) return m_stationJoined;
	}

	return m_last;
}

static void printXmlEscaped(Print &output, const char *value) {
	if (!value) return;

	while (*value) {
		switch (*value) {
			case '&': output.print(F("&amp;")); break;
			case '<': output.print(F("&lt;")); break;
			case '>': output.print(F("&gt;")); break;
			case '"': output.print(F("&quot;")); break;
			case '\'': output.print(F("&apos;")); break;
			default: output.write(static_cast<uint8_t>(*value)); break;
		}
		++value;
	}
}

void SSDPDeviceClass::schema(Print &client) const {
	const IPAddress ip = activeInterfaceIP();

	client.print(F(
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/xml; charset=utf-8\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n"
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
		"<specVersion><major>1</major><minor>1</minor></specVersion>"
		"<URLBase>http://"
	));
	client.print(ip);
	client.print(':');
	client.print(m_port);
	client.print(F("/</URLBase><device><deviceType>"));
	printXmlEscaped(client, m_deviceType);
	client.print(F("</deviceType><friendlyName>"));
	printXmlEscaped(client, m_friendlyName);
	client.print(F("</friendlyName><presentationURL>"));
	printXmlEscaped(client, m_presentationURL);
	client.print(F("</presentationURL><serialNumber>"));
	printXmlEscaped(client, m_serialNumber);
	client.print(F("</serialNumber><modelName>"));
	printXmlEscaped(client, m_modelName);
	client.print(F("</modelName><modelNumber>"));
	printXmlEscaped(client, m_modelNumber);
	client.print(F("</modelNumber><modelURL>"));
	printXmlEscaped(client, m_modelURL);
	client.print(F("</modelURL><manufacturer>"));
	printXmlEscaped(client, m_manufacturer);
	client.print(F("</manufacturer><manufacturerURL>"));
	printXmlEscaped(client, m_manufacturerURL);
	client.print(F("</manufacturerURL><UDN>uuid:"));
	printXmlEscaped(client, m_uuid);
	client.print(F("</UDN></device></root>\r\n"));
}

void SSDPDeviceClass::handleClient() {
	const IPAddress zero(0, 0, 0, 0);

	IPAddress station = zero;
	if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != zero) {
		station = WiFi.localIP();
	}

	IPAddress accessPoint = zero;
	const WiFiMode_t mode = WiFi.getMode();
	if (mode == WIFI_AP || mode == WIFI_AP_STA) {
		accessPoint = WiFi.softAPIP();
	}

	// The ESP8266 lwIP2 IGMP group pool is small and shared with mDNS/LLMNR.
	// Prefer STA and fall back to soft-AP rather than exhausting it by joining
	// SSDP on both at once.
	const IPAddress preferred = station != zero ? station : accessPoint;
	if (preferred == station) {
		accessPoint = zero;
	}
	else {
		station = zero;
	}

	const bool topologyChanged =
		(m_last != zero && m_last != preferred) ||
		(m_stationJoined != zero && m_stationJoined != station) ||
		(m_accessPointJoined != zero && m_accessPointJoined != accessPoint);

	if (topologyChanged) {
		clearQueue();
		if (m_server) {
			sendAllByebye(m_stationJoined);
			if (m_accessPointJoined != m_stationJoined) {
				sendAllByebye(m_accessPointJoined);
			}
			m_server->stop();
			leaveJoinedInterfaces();
		}

		m_last = zero;
		m_stationJoined = zero;
		m_accessPointJoined = zero;
		m_joinRetryAt = 0;
	}

	const uint32_t now = millis();
	if (
		preferred != zero &&
		m_last == zero &&
		(m_joinRetryAt == 0 || static_cast<int32_t>(now - m_joinRetryAt) >= 0)
	) {
		if (!m_server) m_server = new (std::nothrow) WiFiUDP();
		if (m_server) {
			m_server->stop();
			if (m_server->beginMulticast(preferred, SSDP_MULTICAST_ADDR, SSDP_PORT)) {
				m_last = preferred;
				if (preferred == station) m_stationJoined = station;
				if (preferred == accessPoint) m_accessPointJoined = accessPoint;
				m_joinRetryAt = 0;
				postNotifyALive(preferred);
			}
			else {
				// beginMulticast() joins IGMP before opening its UDP context and
				// does not undo the join when listen() fails.
				igmp_leavegroup(preferred, SSDP_MULTICAST_ADDR);
				m_joinRetryAt = now + 1000UL;
			}
		}
		else {
			m_joinRetryAt = now + 1000UL;
		}
	}

	if (m_server && m_last != zero && m_server->parsePacket()) {
		String value;

		if (readLine(value) && value.equalsIgnoreCase("M-SEARCH * HTTP/1.1")) {
			String key, st;
			bool host = false, man = false;
			long mx = 0;

			while (readKeyValue(key, value)) {
				key.trim();
				value.trim();

				if (
					key.equalsIgnoreCase("HOST") &&
					(
						value.equals("239.255.255.250:1900") ||
						value.equals("239.255.255.250")
					)
				) {
					host = true;
				}
				else if (
					key.equalsIgnoreCase("MAN") &&
					value.equalsIgnoreCase("\"ssdp:discover\"")
				) {
					man = true;
				}
				else if (key.equalsIgnoreCase("ST")) {
					st = value;
				}
				else if (key.equalsIgnoreCase("MX")) {
					mx = value.toInt();
				}
			}

			if (host && man && mx > 0) {
				mx = constrain(mx, 1L, 5L);

				if (st.equalsIgnoreCase("ssdp:all")) {
					postResponse(mx);
				}
				else if (st.equalsIgnoreCase("upnp:rootdevice")) {
					postResponse(ROOT_FOR_ALL, mx);
				}
				else if (st.equalsIgnoreCase("uuid:" + String(m_uuid))) {
					postResponse(ROOT_BY_UUID, mx);
				}
				else if (st.equalsIgnoreCase(m_deviceType)) {
					postResponse(ROOT_BY_TYPE, mx);
				}
				else if (
					serviceEnabled() &&
					st.equalsIgnoreCase(FW_UPNP_SERVICE_TYPE)
				) {
					postResponse(SERVICE_BY_TYPE, mx);
				}
			}
		}

		while (m_server->available() > 0) {
			m_server->read();
		}
	}

	if (m_server && m_last != zero) {
		const unsigned long time = millis();
		for (int i = 0; i < SSDP_QUEUE_SIZE; i++) {
			if (
				m_queue[i].time > 0 &&
				static_cast<int32_t>(time - m_queue[i].time) >= 0
			) {
				send(&m_queue[i]);
			}
		}
	}
}

IPAddress SSDPDeviceClass::activeInterfaceIP() const {
	const IPAddress zero(0, 0, 0, 0);
	const IPAddress station = WiFi.localIP();

	if (WiFi.status() == WL_CONNECTED && station != zero) {
		return station;
	}

	const WiFiMode_t mode = WiFi.getMode();
	const IPAddress accessPoint = WiFi.softAPIP();
	if ((mode == WIFI_AP || mode == WIFI_AP_STA) && accessPoint != zero) {
		return accessPoint;
	}

	return zero;
}

void SSDPDeviceClass::setSchemaURL(const char *url) {
	strlcpy(m_schemaURL, url, sizeof(m_schemaURL));
}

void SSDPDeviceClass::setHTTPPort(uint16_t port) {
	m_port = port;
}

void SSDPDeviceClass::setDeviceType(const char *deviceType) {
	strlcpy(m_deviceType, deviceType, sizeof(m_deviceType));
}

void SSDPDeviceClass::setUUID(const char *uuid) {
	snprintf_P(m_uuid, sizeof(m_uuid), PSTR("%s"), uuid);
}

void SSDPDeviceClass::setName(const char *name) {
	strlcpy(m_friendlyName, name, sizeof(m_friendlyName));
}

void SSDPDeviceClass::setURL(const char *url) {
	strlcpy(m_presentationURL, url, sizeof(m_presentationURL));
}

void SSDPDeviceClass::setSerialNumber(const char *serialNumber) {
	strlcpy(m_serialNumber, serialNumber, sizeof(m_serialNumber));
}

void SSDPDeviceClass::setSerialNumber(const uint32_t serialNumber) {
	snprintf(m_serialNumber, sizeof(uint32_t) * 2 + 1, "%08X", serialNumber);
}

void SSDPDeviceClass::setModelName(const char *name) {
	strlcpy(m_modelName, name, sizeof(m_modelName));
}

void SSDPDeviceClass::setModelNumber(const char *num) {
	strlcpy(m_modelNumber, num, sizeof(m_modelNumber));
}

void SSDPDeviceClass::setModelURL(const char *url) {
	strlcpy(m_modelURL, url, sizeof(m_modelURL));
}

void SSDPDeviceClass::setManufacturer(const char *name) {
	strlcpy(m_manufacturer, name, sizeof(m_manufacturer));
}

void SSDPDeviceClass::setManufacturerURL(const char *url) {
	strlcpy(m_manufacturerURL, url, sizeof(m_manufacturerURL));
}

void SSDPDeviceClass::setTTL(const uint8_t ttl) {
	m_ttl = ttl;
}

void SSDPDeviceClass::setInterval(uint32_t interval) {
	m_interval = interval;
}

SSDPDeviceClass SSDPDevice;
