#pragma once

/**
 * @file ProductConfig.h
 * @brief Compile-time product identity and shared firmware feature defaults.
 *
 * Every value may be overridden by a compiler -D flag or by a generated local
 * header included before this file. Keeping product identity here prevents
 * discovery, HTTP, WebSocket, and tooling strings from drifting independently.
 */

#define FW_PRODUCT_PROFILE_PU850      1
#define FW_PRODUCT_PROFILE_RAYAN_LAMP 2

#ifndef FW_PRODUCT_PROFILE
#define FW_PRODUCT_PROFILE FW_PRODUCT_PROFILE_PU850
#endif

#if FW_PRODUCT_PROFILE == FW_PRODUCT_PROFILE_RAYAN_LAMP

#ifndef FW_PRODUCT_ID
#define FW_PRODUCT_ID "rayanlamp"
#endif
#ifndef FW_PRODUCT_NAME
#define FW_PRODUCT_NAME "Rayan Lamp"
#endif
#ifndef FW_PRODUCT_DESCRIPTION
#define FW_PRODUCT_DESCRIPTION "Rayan Lamp ESP8266 Binary Light"
#endif
#ifndef FW_PRODUCT_MODEL_NAME
#define FW_PRODUCT_MODEL_NAME "RayanLamp"
#endif
#ifndef FW_PRODUCT_MODEL_URL
#define FW_PRODUCT_MODEL_URL ""
#endif
#ifndef FW_MANUFACTURER_NAME
#define FW_MANUFACTURER_NAME "Rayan's Lamp"
#endif
#ifndef FW_MANUFACTURER_URL
#define FW_MANUFACTURER_URL ""
#endif
#ifndef FW_HTTP_SERVER_NAME
#define FW_HTTP_SERVER_NAME "RayanLamp"
#endif
#ifndef FW_WEBSOCKET_SERVER_ID
#define FW_WEBSOCKET_SERVER_ID "ESP8266"
#endif
#ifndef FW_MNDP_PLATFORM
#define FW_MNDP_PLATFORM "Rayan's Lamp"
#endif
#ifndef FW_MNDP_BOARD
#define FW_MNDP_BOARD "RayanLamp"
#endif
#ifndef FW_UPNP_DEVICE_TYPE
#define FW_UPNP_DEVICE_TYPE "urn:schemas-upnp-org:device:BinaryLight:1"
#endif

#elif FW_PRODUCT_PROFILE == FW_PRODUCT_PROFILE_PU850

#ifndef FW_PRODUCT_ID
#define FW_PRODUCT_ID "pu850"
#endif
#ifndef FW_PRODUCT_NAME
#define FW_PRODUCT_NAME "PU850"
#endif
#ifndef FW_PRODUCT_DESCRIPTION
#define FW_PRODUCT_DESCRIPTION "PU850 Indicator Device"
#endif
#ifndef FW_PRODUCT_MODEL_NAME
#define FW_PRODUCT_MODEL_NAME "PU850"
#endif
#ifndef FW_PRODUCT_MODEL_URL
#define FW_PRODUCT_MODEL_URL "https://pandcaspian.com/%D8%A7%D9%86%D8%AF%DB%8C%DA%A9%D8%A7%D8%AA%D9%88%D8%B1/"
#endif
#ifndef FW_MANUFACTURER_NAME
#define FW_MANUFACTURER_NAME "Pand Caspian"
#endif
#ifndef FW_MANUFACTURER_URL
#define FW_MANUFACTURER_URL "https://pandcaspian.com/"
#endif
#ifndef FW_HTTP_SERVER_NAME
#define FW_HTTP_SERVER_NAME "PU850"
#endif
#ifndef FW_WEBSOCKET_SERVER_ID
#define FW_WEBSOCKET_SERVER_ID "PU850"
#endif
#ifndef FW_MNDP_PLATFORM
#define FW_MNDP_PLATFORM FW_MANUFACTURER_NAME
#endif
#ifndef FW_MNDP_BOARD
#define FW_MNDP_BOARD FW_PRODUCT_MODEL_NAME
#endif
#ifndef FW_UPNP_DEVICE_TYPE
#define FW_UPNP_DEVICE_TYPE "urn:schemas-upnp-org:device:Basic:1"
#endif

#else
#error "Unsupported FW_PRODUCT_PROFILE"
#endif

#ifndef FW_PRODUCT_INDEX_TEXT
#define FW_PRODUCT_INDEX_TEXT FW_MANUFACTURER_NAME " " FW_PRODUCT_NAME "\n"
#endif

/*
 * UPnP discovery defaults shared by all profiles. A service type is deliberately
 * opt-in: the common firmware does not implement a SOAP/GENA service and must
 * never advertise one merely because a product profile changed its device type.
 * Downstream products can define both the service type and the matching service.
 */
#ifndef FW_UPNP_SERVICE_TYPE
#define FW_UPNP_SERVICE_TYPE ""
#endif
#ifndef FW_UPNP_SCHEMA_PATH
#define FW_UPNP_SCHEMA_PATH "description.xml"
#endif
#ifndef FW_UPNP_CACHE_MAX_AGE_SECONDS
#define FW_UPNP_CACHE_MAX_AGE_SECONDS 1800
#endif
#ifndef FW_UPNP_MULTICAST_TTL
#define FW_UPNP_MULTICAST_TTL 4
#endif
#ifndef FW_UPNP_SERVER_PRODUCT
#define FW_UPNP_SERVER_PRODUCT FW_HTTP_SERVER_NAME
#endif
#ifndef FW_UPNP_SERVER_VERSION
#define FW_UPNP_SERVER_VERSION "1.0"
#endif

#ifndef FW_ENABLE_FIRMWARE_DOWNLOAD
#define FW_ENABLE_FIRMWARE_DOWNLOAD 1
#endif

/*
 * A full-flash dump can contain Wi-Fi credentials and other persisted settings.
 * It is deliberately disabled unless a product build opts in explicitly.
 */
#ifndef FW_ENABLE_FULL_FLASH_DOWNLOAD
#define FW_ENABLE_FULL_FLASH_DOWNLOAD 0
#endif

/*
 * Optional shared bearer token for OTA upload and firmware download. An empty
 * value preserves existing LAN deployments. Production builds should define it
 * in the ignored ~secrets.h file without committing or logging the token.
 */
#ifndef FW_OTA_BEARER_TOKEN
#define FW_OTA_BEARER_TOKEN ""
#endif

/*
 * Network diagnostics should report topology without disclosing saved Wi-Fi
 * credentials. Legacy deployments may explicitly opt back in at build time.
 */
#ifndef FW_EXPOSE_WIFI_CREDENTIALS
#define FW_EXPOSE_WIFI_CREDENTIALS 0
#endif
