#pragma once

#include "../ASA0002E.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

WiFiUDP udp;

// udp address and port
const IPAddress udpAddress(255, 255, 255, 255);
const uint16_t udpPort = 9876;

bool udpRunning = false;

uint32_t previousUdpMillis = 0;

void udpSend();
void handleUdpReceive();
void executeCommand(const char* command);

bool initUdpDebug() {
	if (WiFi.status() != WL_CONNECTED && !WiFi.softAPIP().isSet())
	{
		udpRunning = false;
		udp.stop();

		return udpRunning;
	}

	// Start the UDP server
	udpRunning = udp.begin(udpPort);

	return udpRunning;
}

void UdpService() {
	if ( !udpRunning && !initUdpDebug )
		return;

	handleUdpReceive();

	if (millis() - previousUdpMillis >= 1000)
	{
		previousUdpMillis = millis();

		// udpSend();
	}
}

#define PACKET_SIZE (sizeof(packetBuffer))

void udpSend() {
	char packetBuffer[255];
	memset(packetBuffer, '\0', PACKET_SIZE);
	unsigned int offset = 0;

	memcpy(packetBuffer + offset, "Hello, world!", 14);
	offset += 14;

	udp.beginPacket(udpAddress, udpPort);
	udp.write(packetBuffer, offset);
	udp.endPacket();
}

void handleUdpReceive() {
	// Check if there's any data available
	const size_t packetSize = udp.parsePacket();

	if (packetSize) {
		IPAddress remoteIp = udp.remoteIP();
		uint16_t remotePort = udp.remotePort();

		char packetBuffer[255];
		memset(packetBuffer, '\0', PACKET_SIZE);

		// Read the incoming packet into a buffer
		const size_t bytesRead = udp.read(packetBuffer, PACKET_SIZE - 1);

		if (bytesRead > 0) {
			packetBuffer[bytesRead] = '\0';
			executeCommand(packetBuffer);
		}

		// Send a response back to the client
		String responseMessage = "Command received successfully";
		udp.beginPacket(remoteIp, remotePort);
		udp.write(responseMessage.c_str());
		udp.endPacket();
	}
}

void executeCommand(const char* command) {
	if ( CompStr(command, "hello") == OK_ )
	{
		udpSend();
	}
}

void ServiceEvent(System_Event_t *evt)
{
	switch (evt->event)
	{
		case EVENT_OPMODE_CHANGED:
			initUdpDebug();
			break;

		case EVENT_STAMODE_CONNECTED:
			// os_printf("connected to ssid %s, channel: %d\n",
			// 	evt->event_info.connected.ssid,
			// 	evt->event_info.connected.channel);
			initUdpDebug();
			break;

		case EVENT_STAMODE_DISCONNECTED:
			// os_printf("disconnected from ssid %s, reason: %d\n",
			// 	evt->event_info.disconnected.ssid,
			// 	evt->event_info.disconnected.reason);
			// char ssid[33];
			// memcpy(ssid, evt->event_info.disconnected.ssid, evt->event_info.disconnected.ssid_len);
			// ssid[ evt->event_info.disconnected.ssid_len] = '\0';
			initUdpDebug();
			break;

		// We have been allocated an IP address.
		case EVENT_STAMODE_GOT_IP:
			// os_printf("ip:" IPSTR ", mask:" IPSTR ", gw:" IPSTR,
			// 	IP2STR(&evt->event_info.got_ip.ip),
			// 	IP2STR(&evt->event_info.got_ip.mask),
			// 	IP2STR(&evt->event_info.got_ip.gw));
			// os_printf("\n");
			initUdpDebug();
			break;

		case EVENT_SOFTAPMODE_STACONNECTED:
			// os_printf("station:	" MACSTR "join,	AID = %d\n",
			// 	MAC2STR(evt->event_info.sta_connected.mac),
			// 	evt->event_info.sta_connected.aid);
			initUdpDebug();
			break;

		case EVENT_SOFTAPMODE_STADISCONNECTED:
			// os_printf("station: " MACSTR	" leave, AID = %d\n",
			// 	MAC2STR(evt->event_info.sta_disconnected.mac),
			// 	evt->event_info.sta_disconnected.aid);
			initUdpDebug();
			break;
	}
}
