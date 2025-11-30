#include "../ASA0002E.h"
#include "Commands.h"
#include "Shell.h"
#include "UART.h"

// Define the array of device parameters
const DeviceParameter DeviceParameters[] =
{
	{
		.name = "Hostname",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			AssignResult(args, WiFi.hostname().c_str());
			return OK_;
		},

		.set = [](const C8* args) -> U8 {
			bool success = isValidHostName(args) && WiFi.hostname(args);
			if (success) strncpy(flashSettings.hostname, args, sizeof(flashSettings.hostname));
			onValueUpdate(ESP_Suffix_HostName, 0);
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "WiFi_Mode",
		.type = TYPE_INT,

		.get = [](C8* args) -> U8 {
			AssignResult(args, String((U8) WiFi.getMode()).c_str());
			return OK_;
		},

		.set = [](const C8* args) -> U8 {
			S32 num;
			bool success = NumberFromString(args, &num);
			if (success && (success = WiFi.mode((WiFiMode_t) num))) {
				// U8 wifi_mode = wifi_get_opmode();
			}
			flashSettings.net_flags = getNetFlags();
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "Station_SSID",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct station_config config;
			bool success = wifi_station_get_config(&config);
			const C8* ssid = reinterpret_cast<const char*>(config.ssid);
			AssignResult(args, ssid);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct station_config config;
			bool success = wifi_station_get_config(&config);
			strncpy((C8*)config.ssid, args, sizeof(config.ssid));
			if (success && (success = wifi_station_set_config_current(&config))) {
				strncpy(flashSettings.sta_ssid, args, sizeof(flashSettings.sta_ssid));
				flashSettings.sta_ssid[sizeof(flashSettings.sta_ssid) - 1] = '\0';
			}
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "Station_Password",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct station_config config;
			bool success = wifi_station_get_config(&config);
			const C8* password = reinterpret_cast<const char*>(config.password);
			AssignResult(args, password);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct station_config config;
			bool success = wifi_station_get_config(&config);
			strncpy((C8*)config.password, args, sizeof(config.password));
			if (success && (success = wifi_station_set_config_current(&config))) {
				strncpy(flashSettings.sta_password, args, sizeof(flashSettings.sta_password));
				flashSettings.sta_password[sizeof(flashSettings.sta_password) - 1] = '\0';
			}
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "AP_SSID",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct softap_config config;
			bool success = wifi_softap_get_config(&config);
			const C8* ssid = reinterpret_cast<const char*>(config.ssid);
			AssignResult(args, ssid);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct softap_config config;
			bool success = wifi_softap_get_config(&config);
			strncpy((C8*)config.ssid, args, sizeof(config.ssid));
			config.ssid_len = strlen(args);
			if (success && (success = wifi_softap_set_config_current(&config))) {
				strncpy(flashSettings.ap_ssid, args, sizeof(flashSettings.ap_ssid));
				flashSettings.ap_ssid[sizeof(flashSettings.ap_ssid) - 1] = '\0';
			}
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "AP_Password",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct softap_config config;
			bool success = wifi_softap_get_config(&config);
			const C8* password = reinterpret_cast<const char*>(config.password);
			AssignResult(args, password);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct softap_config config;
			bool success = wifi_softap_get_config(&config);
			strncpy((C8*)config.password, args, sizeof(config.password));
			if (success && (success = wifi_softap_set_config_current(&config))) {
				strncpy(flashSettings.ap_password, args, sizeof(flashSettings.ap_password));
				flashSettings.ap_password[sizeof(flashSettings.ap_password) - 1] = '\0';
			}
			return success ? OK_ : NotOK_;
		},
	},

	/*
	{
		.name = "AP_IP",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			const C8* ip = reinterpret_cast<const char*>(&info.ip.addr);
			AssignResult(args, ip);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			ip_addr_t ip;
			ip.addr = ipaddr_addr(args);
			info.ip = ip;
			if (success) success = wifi_set_ip_info(SOFTAP_IF, &info);
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "AP_Gateway",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			const C8* gateway = reinterpret_cast<const char*>(&info.gw.addr);
			AssignResult(args, gateway);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			ip_addr_t gateway;
			gateway.addr = ipaddr_addr(args);
			info.gw = gateway;
			if (success) success = wifi_set_ip_info(SOFTAP_IF, &info);
			return success ? OK_ : NotOK_;
		},
	},

	{
		.name = "AP_Subnet",
		.type = TYPE_STRING,

		.get = [](C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			const C8* subnet = reinterpret_cast<const char*>(&info.netmask.addr);
			AssignResult(args, subnet);
			return success ? OK_ : NotOK_;
		},

		.set = [](const C8* args) -> U8 {
			struct ip_info info;
			bool success = wifi_get_ip_info(SOFTAP_IF, &info);
			ip_addr_t subnet;
			subnet.addr = ipaddr_addr(args);
			info.netmask = subnet;
			if (success) success = wifi_set_ip_info(SOFTAP_IF, &info);
			return success ? OK_ : NotOK_;
		},
	},
	*/

	// ─────────────────────────────────────────────────────────────────────────

	{
		.name = "Uptime",
		.type = TYPE_STRING | FLAG_READONLY,

		.get = [](C8* args) -> U8 {
			AssignResult(args, getFormattedUptime(millis() / 1000).c_str());
			return OK_;
		},
	},

	{
		.name = "Firmware_Hash",
		.type = TYPE_STRING | FLAG_READONLY,

		.get = [](C8* args) -> U8 {
			AssignResult(args, ESP.getSketchMD5().c_str());
			return OK_;
		},
	},

	{
		.name = "Build_Date",
		.type = TYPE_STRING | FLAG_READONLY,

		.get = [](C8* args) -> U8 {
			AssignResult(args, __DATE__ " " __TIME__);
			return OK_;
		},
	}

};

// Map of command-function pairs
const CommandEntry commandMap[] = {
	{"echo",    [](const C8* args) -> void { String_NewLine_ToShell(args); }},
	// {"strlen",  [](const C8* args) -> void { String_Num_NewLine_ToShell("Length = ", strlen(args)); }},

	// {"file",    [](const C8* args) -> void { String_Num_NewLine_ToShell("Result = ", (S32)Request_GiveFile(args)); WaitForBulkDataComplete(); }},

	{"save",    [](const C8* args) -> void { String_NewLine_ToShell(Settings_Write() ? "Saved" : "Failed"); }},
	{"restore", [](const C8* args) -> void { String_NewLine_ToShell(Settings_Read()  ? "OK" : (settingsError == 1 ? "Empty data" : (settingsError == 2 ? "CRC Mismatch" : "Unknown Error"))); }},
	{"connect", [](const C8* args) -> void { WiFi_Setup(); }},

	{"reboot",  [](const C8* args) -> void { Shell_clearScreen(); String_NewLine_ToShell("\e[1;33m" "Rebooting..." "\e[0m"); Shell_exit(); ESP.restart(); }},
	{"busy",    [](const C8* args) -> void { SendESPStatus_ToPU(Busy_, true);  String_NewLine_ToShell("OK"); }},
	{"ready",   [](const C8* args) -> void { SendESPStatus_ToPU(Ready_, true); String_NewLine_ToShell("OK"); }},

	{"status",  [](const C8* args) -> void {
		station_status_t status = wifi_station_get_connect_status();

		switch (status) {
			case STATION_IDLE:           String_NewLine_ToShell("Station Idle");           break;
			case STATION_CONNECTING:     String_NewLine_ToShell("Station Connecting");     break;
			case STATION_WRONG_PASSWORD: String_NewLine_ToShell("Station Wrong Password"); break;
			case STATION_NO_AP_FOUND:    String_NewLine_ToShell("Station No AP Found");    break;
			case STATION_CONNECT_FAIL:   String_NewLine_ToShell("Station Connect Fail");   break;
			case STATION_GOT_IP:         String_NewLine_ToShell("Station Got IP");         break;
			default:                     String_NewLine_ToShell("Unknown Status");         break;
		}
	}},
};


// Define the number of parameters
const U8 numParameters = count_of(DeviceParameters);

// Function to get the device parameter ID by name
U8 getDeviceParameterIdByName(const C8* paramName) {
	for (U8 i = 0; i < numParameters; i++) {
		if ( CompStr(DeviceParameters[i].name, paramName) == OK_ ) {
			return i;
		}
	}

	return 0xff; // Parameter name not found
}
