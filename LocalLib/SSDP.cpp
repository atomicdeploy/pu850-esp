#include "../ASA0002E.h"

#include "SSDP.h"
#include "SSDPDevice.cpp"

static void appendXmlEscaped(String &output, const char *value)
{
	if (!value) return;

	while (*value) {
		switch (*value) {
			case '&': output += F("&amp;"); break;
			case '<': output += F("&lt;"); break;
			case '>': output += F("&gt;"); break;
			case '"': output += F("&quot;"); break;
			case '\'': output += F("&apos;"); break;
			default: output += *value; break;
		}
		++value;
	}
}

String getSSDPSchema()
{
	String schema;
	if (!schema.reserve(2048)) return schema;

	schema += F(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
		"\t<specVersion><major>1</major><minor>1</minor></specVersion>\n"
		"\t<URLBase>http://"
	);

	const IPAddress activeIP = SSDPDevice.activeInterfaceIP();
	if (activeIP == IPAddress(0, 0, 0, 0)) {
		const String hostname = WiFi.hostname();
		appendXmlEscaped(schema, hostname.c_str());
	}
	else {
		schema += activeIP.toString();
	}
	schema += ':';
	schema += HTTP_Port;
	schema += F("/</URLBase>\n\t<device>\n\t\t<deviceType>");
	appendXmlEscaped(schema, FW_UPNP_DEVICE_TYPE);
	schema += F("</deviceType>\n\t\t<friendlyName>");
	const String hostname = WiFi.hostname();
	appendXmlEscaped(schema, hostname.c_str());
	schema += F("</friendlyName>\n\t\t<manufacturer>");
	appendXmlEscaped(schema, FW_MANUFACTURER_NAME);
	schema += F("</manufacturer>\n\t\t<manufacturerURL>");
	appendXmlEscaped(schema, FW_MANUFACTURER_URL);
	schema += F("</manufacturerURL>\n\t\t<modelDescription>");
	appendXmlEscaped(schema, FW_PRODUCT_DESCRIPTION);
	schema += F("</modelDescription>\n\t\t<modelName>");
	appendXmlEscaped(schema, FW_PRODUCT_MODEL_NAME);
	schema += F("</modelName>\n\t\t<modelNumber>");

	if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0) {
		appendXmlEscaped(schema, firmwareVersion.c_str());
	}
	else {
		appendXmlEscaped(schema, E_MainVersion);
	}

	schema += F("</modelNumber>\n\t\t<modelURL>");
	appendXmlEscaped(schema, FW_PRODUCT_MODEL_URL);
	schema += F("</modelURL>\n\t\t<serialNumber>");

	const String serialNumber =
		E_SerialNumber == ErrorNum_ || E_SerialNumber >= UnDefinedNum_
			? F("Unknown")
			: String(E_SerialNumber);
	appendXmlEscaped(schema, serialNumber.c_str());

	schema += F(
		"</serialNumber>\n"
		"\t\t<presentationURL>/</presentationURL>\n"
		"\t\t<UDN>uuid:"
	);
	appendXmlEscaped(schema, SSDPDevice.uuid());
	schema += F("</UDN>\n\t</device>\n</root>\n");

	/*
	 * Intentionally no serviceList here. FW_UPNP_SERVICE_TYPE controls SSDP
	 * discovery only; a downstream product must provide the matching SOAP/GENA
	 * implementation and description before opting in.
	 */
	return schema;
}

void initSSDP()
{
	SSDPDevice.setSchemaURL(FW_UPNP_SCHEMA_PATH);
	SSDPDevice.setHTTPPort(HTTP_Port);
	SSDPDevice.setName(WiFi.hostname().c_str());
	SSDPDevice.setDeviceType(FW_UPNP_DEVICE_TYPE);
	SSDPDevice.setSerialNumber(String(E_SerialNumber).c_str()); // ESP.getChipId()
	SSDPDevice.setModelName(FW_PRODUCT_MODEL_NAME);
	SSDPDevice.setModelNumber(E_MainVersion);
	SSDPDevice.setModelURL(FW_PRODUCT_MODEL_URL);
	SSDPDevice.setManufacturer(FW_MANUFACTURER_NAME);
	SSDPDevice.setManufacturerURL(FW_MANUFACTURER_URL);
	SSDPDevice.setURL("/");
	SSDPDevice.setInterval(FW_UPNP_CACHE_MAX_AGE_SECONDS);

	if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0) {
		// If the version is not set, use the sketch MD5 as a fallback
		SSDPDevice.setModelNumber(firmwareVersion.c_str());
	}

	server->on("/" FW_UPNP_SCHEMA_PATH, HTTP_GET, [](AsyncWebServerRequest * request) {
		const String schema = getSSDPSchema();

		if (schema.length() == 0) {
			request->send(500, "text/plain", "server busy");
			return;
		}

		AsyncWebServerResponse *response =
			request->beginResponse(200, "text/xml; charset=utf-8", schema);
		response->addHeader("Access-Control-Allow-Origin", "*");
		response->addHeader(
			"SERVER",
			"ESP8266/1.0 UPnP/1.1 " FW_UPNP_SERVER_PRODUCT "/" FW_UPNP_SERVER_VERSION
		);
		request->send(response);
	});

	SSDPDevice.begin();
}

inline void SSDP_Service()
{
	SSDPDevice.handleClient();
}
