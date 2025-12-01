#pragma once

/**
 * @file MNDP.h
 * @brief MikroTik Neighbor Discovery Protocol (MNDP) implementation
 *
 * Implements MNDP for device discovery on local networks.
 * Allows the device to be discovered by MikroTik routers and
 * network management tools like Winbox.
 */

#include "../ASA0002E.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

WiFiUDP MNDP;

// MNDP multicast address and port
const IPAddress mndpAddress(224, 0, 0, 88);
const unsigned int mndpPort = 5678;

bool mndpRunning = false;
uint32_t counterMNDP = 0;

// TLV Types for MNDP
typedef enum {
	TLV_TYPE_MAC_ADDRESS = 1,
	TLV_TYPE_IDENTITY    = 5,
	TLV_TYPE_VERSION     = 7,
	TLV_TYPE_PLATFORM    = 8,
	TLV_TYPE_UPTIME      = 10,
	TLV_TYPE_SOFTWARE_ID = 11,
	TLV_TYPE_BOARD       = 12,
	TLV_TYPE_UNPACK      = 14,
	TLV_TYPE_IPV6_ADDR   = 15,
	TLV_TYPE_INTERFACE   = 16,
	TLV_TYPE_IPV4_ADDR   = 17,
} tlv_type_t;

unsigned int addTlv(uint8_t *buffer, uint8_t type, uint8_t length, const void *value);
void addValueToBuffer(uint8_t* buffer, size_t offset, uint32_t value);
void setupMNDP();
void sendMNDP();
