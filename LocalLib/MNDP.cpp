#include "MNDP.h"

// add a TLV to the buffer
unsigned int addTlv(uint8_t *buffer, uint8_t type, uint8_t length, const void *value) {
	unsigned int i = 0;
	buffer[i++] = 0;
	buffer[i++] = type;
	buffer[i++] = 0;
	buffer[i++] = length;
	memcpy(buffer + i, value, length);
	return length + i; // Total TLV size
}

// add a 32-bit value to the buffer
void addValueToBuffer(uint8_t* buffer, size_t offset, uint32_t value) {
	uint8_t bytes[sizeof(value)];
	memcpy(bytes, &value, sizeof(value));

	for (size_t i = 0; i < sizeof(value); i++) {
		buffer[offset + i] = bytes[sizeof(value) - 1 - i];
	}
}

void setupMNDP() {
	if (WiFi.status() != WL_CONNECTED)
	{
		mndpRunning = false;
		MNDP.stop();
		return;
	}

	// Start the UDP server
	mndpRunning = MNDP.begin(mndpPort); // MNDP.beginMulticast(WiFi.localIP(), mndpAddress, mndpPort);

	if (firmwareVersion[0] == Null_)
		firmwareVersion = ESP.getSketchMD5();
}

void sendMNDP() {
	// Create the MNDP packet
	uint8_t buffer[255];
	unsigned int offset = 0;

	// Add MNDP header
	addValueToBuffer(buffer, offset, counterMNDP++);
	offset += sizeof(counterMNDP);

	// Add MAC address TLV
	uint8_t mac[6];
	WiFi.macAddress(mac);
	offset += addTlv(buffer + offset, TLV_TYPE_MAC_ADDRESS, sizeof(mac), mac);

	// Add Identity TLV
	const char *identity = flashSettings.hostname;
	offset += addTlv(buffer + offset, TLV_TYPE_IDENTITY, strlen(identity), identity);

	// Add Version TLV
	const char *version = strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0 ? firmwareVersion.c_str() : E_MainVersion;
	offset += addTlv(buffer + offset, TLV_TYPE_VERSION, strlen(version), version);

	// Add Platform TLV
	const char *platform = "Pand Caspian";
	offset += addTlv(buffer + offset, TLV_TYPE_PLATFORM, strlen(platform), platform);

	// Add Uptime TLV
	const uint32_t uptime = millis() / 1000;
	offset += addTlv(buffer + offset, TLV_TYPE_UPTIME, sizeof(uptime), &uptime);

	String serialNumber = String(E_SerialNumber == ErrorNum_ || E_SerialNumber >= UnDefinedNum_ ? String("Unknown") : String(E_SerialNumber));

	// Add Software-ID TLV
	const char *software_id = serialNumber.c_str();
	offset += addTlv(buffer + offset, TLV_TYPE_SOFTWARE_ID, strlen(software_id), software_id);

	// Add Board TLV
	const char *board = "PU850";
	offset += addTlv(buffer + offset, TLV_TYPE_BOARD, strlen(board), board);

	// Add Unpack TLV
	// uint8_t unpack = 0;
	// offset += addTlv(buffer + offset, TLV_TYPE_UNPACK, sizeof(unpack), &unpack);

	// Add IPv4 TLV
	const uint32_t ipv4 = WiFi.localIP();
	offset += addTlv(buffer + offset, TLV_TYPE_IPV4_ADDR, sizeof(ipv4), &ipv4);

	const String interfaceStr = WiFi.SSID();

	// Add Interface TLV
	const char *interface = interfaceStr.c_str();
	offset += addTlv(buffer + offset, TLV_TYPE_INTERFACE, strlen(interface), interface);

	// Send the MNDP packet
	MNDP.beginPacketMulticast(mndpAddress, mndpPort, WiFi.localIP());
	MNDP.write(buffer, offset);
	MNDP.endPacket();
}
