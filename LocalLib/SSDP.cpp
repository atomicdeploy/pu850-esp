#include "../ASA0002E.h"

#include "SSDP.h"
#include "SSDPDevice.cpp"

String getSSDPSchema()
{
	String s = "";

	if ( !s.reserve(2048) ) return s;

	s += "<?xml version=\"1.0\"?>\n";
	s += "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n";
	s += "\t<specVersion>\n";
	s += "\t\t<major>1</major>\n";
	s += "\t\t<minor>0</minor>\n";
	s += "\t</specVersion>\n";

	s += "\t<URLBase>";
	s += "http://" + WiFi.hostname() + ":" + String(HTTP_Port) + "/";
	s += "</URLBase>\n";

	s += "\t<device>\n";

	// s += "\t\t<pnpx:X_deviceCategory xmlns:pnpx=\"http://schemas.microsoft.com/windows/pnpx/2005/11\">IndicatorDevice</pnpx:X_deviceCategory>\n";
	// s += "\t\t<df:X_deviceCategory xmlns:df=\"http://schemas.microsoft.com/windows/2008/09/devicefoundation\">IndicatorDevice</df:X_deviceCategory>\n";
	// s += "\t\t<dlna:X_DLNADOC xmlns:dlna="urn:schemas-dlna-org:device-1-0">DMR-1.50</dlna:X_DLNADOC>\n";

	// s += "\t\t<deviceType>upnp:rootdevice</deviceType>\n";
	s += "\t\t<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>\n";

	s += "\t\t<friendlyName>";
	s += WiFi.hostname();
	s += "</friendlyName>\n";

	s += "\t\t<presentationURL>";
	s += "/"; // This is the root URL of the device
	s += "</presentationURL>\n";

	s += "\t\t<modelName>";
	s += "PU850";
	s += "</modelName>\n";

	s += "\t\t<modelURL>";
	s += "https://pandcaspian.com/%D8%A7%D9%86%D8%AF%DB%8C%DA%A9%D8%A7%D8%AA%D9%88%D8%B1/";
	s += "</modelURL>\n";

	s += "\t\t<modelDescription>";
	s += "PU850 Indicator Device";
	s += "</modelDescription>\n";

	s += "\t\t<modelNumber>";
	if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0)
		s += firmwareVersion; // "Unknown";
	else	s += String(E_MainVersion);
	/*
	s += 1; // major
	s += ".";
	s += 0; // minor
	*/
	s += "</modelNumber>\n";

	String serialNumber = String(E_SerialNumber == ErrorNum_ || E_SerialNumber >= UnDefinedNum_ ? String("Unknown") : String(E_SerialNumber));

	s += "\t\t<serialNumber>";
	s += String(serialNumber);
	s += "</serialNumber>\n";

	s += "\t\t<manufacturer>";
	s += "Pand Caspian";
	s += "</manufacturer>\n";

	s += "\t\t<manufacturerURL>";
	s += "https://pandcaspian.com/";
	s += "</manufacturerURL>\n";

	char uuid[37];
	uint32_t chipId = ESP.getChipId();
	memset(uuid, '\0', sizeof(uuid));
	sprintf_P(uuid, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"),
		(uint16_t) ((chipId >> 16) & 0xff),
		(uint16_t) ((chipId >>  8) & 0xff),
		(uint16_t)   chipId        & 0xff);

	s += "\t\t<UDN>uuid:";
	s += String(uuid);
	s += "</UDN>\n";

	s += "\t</device>\n";
	s += "</root>\n";

	return s;
}

/*
StreamString output;
if (output.reserve(1024))
{
	uint32_t ip = WiFi.localIP();
	uint32_t chipId = ESP.getChipId();
	output.printf(ssdpTemplate,
		IP2STR(&ip),
		hostName,
		chipId,
		modelName,
		modelNumber,
		(uint8_t) ((chipId >> 16) & 0xff),
		(uint8_t) ((chipId >>  8) & 0xff),
		(uint8_t)   chipId        & 0xff
	);
	request->send(200, "text/xml", (String)output);
} else {
	request->send(500);
}
*/

/*
static const char* ssdpTemplate =
	"<?xml version=\"1.0\"?>"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
		"<specVersion>"
			"<major>1</major>"
			"<minor>0</minor>"
		"</specVersion>"
		"<URLBase>http://%u.%u.%u.%u/</URLBase>"
		"<device>"
		"<deviceType>upnp:rootdevice</deviceType>"
		// "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"

		"<friendlyName>%s</friendlyName>"

		// s += "\t\t<friendlyName>";
		// s +=_device->friendlyName();
		// s += "</friendlyName>\r\n";

		"<presentationURL>%s</presentationURL>"

		// if (strlen(_device->presentationURL())) {
		// 	s += "\t<presentationURL>";
		// 	s +=_device->presentationURL();
		// 	s += "</presentationURL>\r\n";
		// }

		"<modelName>%s</modelName>"
		// s += "\t\t<modelName>";
		// s +=_device->modelName();
		// s += "</modelName>\r\n";

		"<modelNumber>%s</modelNumber>"

		// if (modelNumber->major > 0 || modelNumber->minor > 0) {
		// 	s += "\t\t<modelNumber>";
		// 	s +=modelNumber->major;
		// 	s += ".";
		// 	s +=modelNumber->minor;
		// 	s += "</modelNumber>\r\n";
		// }

		"<serialNumber>%u</serialNumber>"

		// if (strlen(_device->serialNumber())) {
		// 	s += "\t\t<serialNumber>";
		// 	s +=_device->serialNumber();
		// 	s += "</serialNumber>\r\n";
		// }

		// "<modelURL>http://www.espressif.com</modelURL>"

		// "<manufacturer>Espressif Systems</manufacturer>"

		// s += "\t\t<manufacturer>";
		// s +=_device->manufacturer();
		// s += "</manufacturer>\r\n";

		// "<manufacturerURL>http://www.espressif.com</manufacturerURL>"

		// if (strlen(_device->manufacturerURL())) {
		// 	s += "\t\t<manufacturerURL>";
		// 	s +=_device->manufacturerURL();
		// 	s += "</manufacturerURL>\r\n";
		// }

		// "<UDN>uuid:38323636-4558-4dda-9188-cda0e6%02x%02x%02x</UDN>"

		// s += "\t\t<UDN>uuid:";
		// s +=_device->uuid();
		// s += "</UDN>\r\n";

		"</device>"
	"</root>\r\n"
	"\r\n";
*/

void initSSDP()
{
	SSDPDevice.setSchemaURL("description.xml");
	SSDPDevice.setHTTPPort(HTTP_Port);
	SSDPDevice.setName(WiFi.hostname().c_str());
	// SSDPDevice.setDeviceType("upnp:rootdevice");
	SSDPDevice.setDeviceType("urn:schemas-upnp-org:device:Basic:1");
	SSDPDevice.setSerialNumber(String(E_SerialNumber).c_str()); // ESP.getChipId()
	SSDPDevice.setModelName("PU850");
	SSDPDevice.setModelNumber(E_MainVersion);
	SSDPDevice.setModelURL("https://pandcaspian.com/%D8%A7%D9%86%D8%AF%DB%8C%DA%A9%D8%A7%D8%AA%D9%88%D8%B1/");
	SSDPDevice.setManufacturer("Pand Caspian");
	SSDPDevice.setManufacturerURL("https://pandcaspian.com/");
	SSDPDevice.setURL("/");
	SSDPDevice.setInterval(60);

	if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0) {
		// If the version is not set, use the sketch MD5 as a fallback
		SSDPDevice.setModelNumber(firmwareVersion.c_str());
	}

	// SSDP schema
	server->on("/description.xml", HTTP_GET, [](AsyncWebServerRequest * request) {
		// SSDPDevice.schema(HTTP.client());
		const String schema = getSSDPSchema();

		if (schema.length() == 0) {
			request->send(500, "text/plain", "server busy");
			return;
		}

		// Add CORS header to allow cross-origin requests
		AsyncWebServerResponse *response = request->beginResponse(200, "text/xml", schema);
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});

	SSDPDevice.begin();
}

inline void SSDP_Service()
{
	SSDPDevice.handleClient();
}
