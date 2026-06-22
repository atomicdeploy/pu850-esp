#pragma once

#ifndef LWIP_OPEN_SRC
#define LWIP_OPEN_SRC
#endif

#include "ASA0002E.ino.globals.h"

#define DebugTools
// #define DEBUG_SIZE 8192

// #define ShellOnSerial
// #define DebugOnSerial

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP_EEPROM.h>
#include "cont.h"
#include "osapi.h"
#include "time.h"
#include "ets_sys.h"
#include "user_interface.h"
#include "wl_definitions.h"
#include "esp8266_peri.h"
#include "pgmspace.h"
#include "coredecls.h"
#include "IPAddress.h"
#include "defines.h"

#include <libb64/cencode.h>
#include <libb64/cdecode.h>

#include "LocalLib/Shell.h"
#include "LocalLib/Telnet.h"
#include "LocalLib/SSDP.h"
#include "LocalLib/AsyncWebServer.h"
#include "LocalLib/Public.h"
#include "LocalLib/UART.h"
#include "LocalLib/CrashHandler.h"
#include "LocalLib/Sessions.h"
#include "LocalLib/Authentication.h"

#include "LocalLib/Public.cpp"

// ---------------------------------------------------------------------

struct flashSettings
{
	U32 crc32;				// 4 bytes, must always come first

	U8 net_flags;
	C8 hostname[FCSTS_ + 1];

	C8 ap_ssid[FCSTS_ + 1];
	C8 ap_password[FCSTS_ + 1];

	C8 sta_ssid[FCSTS_ + 1];
	C8 sta_password[FCSTS_ + 1];

	// TODO: static ip settings, port settings
}
flashSettings;

void onBeepReceived(U32 num);
void onMessageReceived(U8 id, U8 title, U8 buttons, U8 icon);
void onWsReceivedCommand(AsyncWebSocketClient *client, const C8* data);
void ConfirmSettings(U8 save, U8 section);

void onValueUpdate(U8 Suffix, U32 Value);
void onResponseReceived(U8 Prefix, U8 Suffix, U16 Response);

U32 getMacAddress(U8 interface, bool part);
bool isValidHostName(C8 *str);

bool setNetFlags(U8 n);
U32 getNetFlags();
U8 getStationStatus();
U8 getAPStatus();

U8 getDHCPStatus();
bool isDHCPTimeout = false;

void WiFi_Setup();
bool Settings_Read();
bool Settings_Write();
U8 settingsError = 0;
bool resetChecked = false;
bool firmwareIsValid = false;
String firmwareVersion = "";

void StreamBulkDataAsResponse(AsyncWebServerRequest *request);

#define ModeIsSet(mode,bit) ((mode & bit) != 0)
#define SetMode(mode,bit) (mode |= bit)
#define ClearMode(mode,bit) (mode &= ~bit)
#define ToggleMode(mode,bit,set) ((set)?(SetMode(mode,bit)):(ClearMode(mode,bit)))
#define getNetStatus() PackU32( 0, getAPStatus(), 0, getStationStatus() )

U8 BSSID[6] = {0, 0, 0, 0, 0, 0};
U8 oldRSSI = 0xff;

U8 lastMasterStatus = 0xff;
U16 lastWsCount = 0;
S32 lastWsWeightValue = 0;
U32 lastWeightReceived = 0;
U32 lastWeightSent = 0;
U32 last1sMillis = 0;
U32 last100msMillis = 0;

// U8 lastWiFiStatus = 0xff;

U8 updateProgress = 0xff, lastUpdateProgress = 0xff;

// ---------------------------------------------------------------------

// Macro to get the number of elements in an array
#define count_of(x) (sizeof(x) / sizeof(x[0]))

// Macro to ensure that an int is <= max and >= min
#define is_between(x, min, max) ((x) >= (min) && (x) <= (max))

// Macro to test if a character is a hexadecimal character (0-9, a-f, A-F)
#define is_hex_char(c) (is_between(c, '0', '9') || is_between(c, 'a', 'f') || is_between(c, 'A', 'F'))

/**
 * @brief Converts an array of bytes to a string that represents the hexadecimal values.
 *
 * @param hexArray The array of bytes.
 * @param length The length of the array.
 * @return The string representation of the hexadecimal values.
 */
String convertToHexString(const C8* hexArray, U16 length) {
	String hexString = "";
	hexString.reserve(length * 2);
	for (size_t i = 0; i < length; i++) {
		if (hexArray[i] < 0x10) hexString += '0';
		hexString += String(hexArray[i], HEX);
	}
	return hexString;
}

/**
 * @brief Parses a string that contains hexadecimal characters to convert them into a byte array.
 *
 * @param hexString The hexadecimal string to parse.
 * @param output The output byte array where the parsed values will be stored.
 * @return True if the parsing is successful, false otherwise.
 */
bool parseHexString(const String& hexString, C8* output) {
	const size_t length = hexString.length();

	if (length % 2 != 0) return false; // invalid hex string length

	char byteValue[3];
	// byteValue[2] = Null_;
	memset(byteValue, Null_, sizeof(byteValue));

	for (size_t i = 0; i < length; i += 2) {
		byteValue[0] = hexString[i];
		byteValue[1] = hexString[i + 1];

		if (!is_hex_char(byteValue[0]) || !is_hex_char(byteValue[1]))
			return false;

		output[i / 2] = strtol(byteValue, nullptr, 16);
	}

	return true;
}

/**
 * @brief Reflect (reverse) all 32 bits of a value.
 *
 * @param value Input value.
 * @return Bit-reflected return.
 */
U32 reflectU32(U32 value) {
	U32 r = 0;
	for (U8 i = 0; i < sizeof(r); i++) {
		if (value & (1u << i))
			r |= (1u << (31 - i));
	}
	return r;
}

/**
 * @brief Calculates the non-reflected (MSB-first) CRC-32/MPEG-2 checksum for the given data, used to ensure data integrity.
 *
 * @param data Pointer to the data buffer.
 * @param length Length of the data buffer.
 * @return The CRC-32/MPEG-2 checksum of the data.
 */
U32 calculateCRC32(const U8 *data, size_t length) {
	U32 crc = 0xFFFFFFFF;

	while (length--) {
		const U8 c = *data++;

		for (U8 i = 0x80; i > 0; i >>= 1) {
			bool bit = crc & 0x80000000;

			if (c & i) bit = !bit;

			crc <<= 1;

			if (bit) crc ^= 0x04C11DB7;
		}
	}

	return crc;
}

/**
 * @brief Calculates the standard (LSB-first) CRC-32 (IEEE 802.3) checksum for the given data, used to ensure data integrity.
 *
 * @param data Pointer to the data buffer.
 * @param length Length of the data buffer.
 * @return The standard CRC-32 checksum of the data.
 */
U32 calculateReflectedCRC32(const U8 *data, size_t length) {
	U32 crc = 0xFFFFFFFF;

	for (size_t i = 0; i < length; i++) {
		const U8 c = data[i];

		crc ^= c;
		for (U8 j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc = crc >> 1;
		}
	}

	return ~crc;
}

/**
 * @brief Calculates the MD5 hash of the given data.
 *
 * This function calculates the MD5 hash of the input data and stores the result in the output buffer.
 * The output buffer should have a size of at least 33 bytes.
 *
 * The MD5 hash is a 128-bit hash value that is represented as a 32-character hexadecimal number.
 *
 * @param data Pointer to the input data.
 * @param len Length of the input data.
 * @param output Pointer to the output buffer where the MD5 hash will be stored.
 * @return True if the MD5 hash was successfully stored, false otherwise.
 */
bool calculateMD5(const U8* data, size_t length, C8* output) {
	md5_context_t _ctx;
	U8* _buf = (U8*) malloc(16);

	if (_buf == NULL) return false;

	memset(_buf, 0x00, 16);

	MD5Init(&_ctx);
	MD5Update(&_ctx, data, length);
	MD5Final(_buf, &_ctx);

	// output has to be 33 bytes long or more
	for (U8 i = 0; i < 16; i++)
		sprintf(output + (i * 2), "%02x", _buf[i]);

	free(_buf);

	return true;
}

/**
 * @brief Generates a random MD5 hash and returns it as a String.
 * The hash is generated using a random 32-bit number.
 * The MD5 hash is a 32-character hexadecimal number.
 *
 * @return The randomly generated MD5 hash as a String if successful, otherwise an empty String.
 */
String getRandomMD5() {
	const U32 r = RANDOM_REG32;

	C8* out = (C8*) malloc(33);

	if (out == NULL || !calculateMD5((U8*)(&r), sizeof(r), out))
		return "";

	return String(out);
}

/**
 * @brief Calculates the MD5 hash of a given String.
 *
 * @param in The input String to calculate the MD5 hash for.
 * @return The MD5 hash of the input string as a String object if successful, otherwise an empty String.
 */
String stringMD5(const String& in) {
	C8* out = (C8*) malloc(33);

	if (out == NULL || !calculateMD5((U8*)(in.c_str()), in.length(), out))
		return "";

	return String(out);
}

/**
 * @brief Checks if the given string is a valid hostname according to RFC952.
 *
 * A hostname is a string of labels separated by dots, each label must be 1-24 characters long.
 * The entire hostname must be 1-255 characters long.
 * The hostname must start with an alpha character and end with an alpha or numeric character.
 *
 * Specification: RFC952
 * Length: 24 characters
 * Characters: alphabet (A-Z), digits (0-9), minus sign (-), and period (.)
 * No distinction is made between upper and lower case.
 * The first character must be an alpha character.
 * The last character must not be a minus sign or period.
 * No blank or space characters are permitted as part of a name.
 *
 * @param str The hostname to be validated.
 * @return True if the hostname was valid, false otherwise.
 */
bool isValidHostName(const C8 *str)
{
	size_t i = 0;

	while (str[i] != Null_)
	{
		if (!is_between(str[i], 'a', 'z') && !is_between(str[i], 'A', 'Z'))
		{
			if (i == 0)
				return false;

			if (!is_between(str[i], '0', '9') && !(str[i] == '-' || str[i] == '.'))
				return false;
		}

		if (i++ > 24)
			return false;
	}

	if (i > 0)
	{
		if (str[i - 1] == '-' || str[i - 1] == '.')
			return false;
	}

	return i != 0;
}

/**
 * @brief Encodes a String into a URL-encoded format.

 * This function replaces all characters except alphanumeric, periods, underscores and dashes with their URL-encoded values.
 * The URL-encoded value is a percent sign (%) followed by two hexadecimal digits.
 * The space character is replaced with a plus sign (+).
 *
 * @param str The String to be encoded
 * @return The URL-encoded string (as a String object)
 */
String urlEncode(const String& str) {
	String encodedString = "";
	C8 c, code0, code1;
	const C8* strPtr = str.c_str();

	while ((c = *strPtr++) > Null_) {
		if (c == ' ') {
			encodedString += '+';
		} else if (isalnum(c) || c == '-' || c == '.' || c == '_') {
			encodedString += c;
		} else {
			code1 = (c & 0xf) + '0';
			if ((c & 0xf) > 9) {
				code1 = (c & 0xf) - 10 + 'A';
			}
			c = (c >> 4) & 0xf;
			code0 = c + '0';
			if (c > 9) {
				code0 = c - 10 + 'A';
			}
			encodedString += '%';
			encodedString += code0;
			encodedString += code1;
		}
	}

	return encodedString;
}

/**
 * @brief Extracts the basename from a given file path.
 *
 * @param filepath The file path to extract the basename from
 * @return The extracted basename
 */
const C8* getBasename(const C8* filepath) {
	const C8* basename = strrchr(filepath, '/');
	if (basename == NULL) {
		basename = strrchr(filepath, '\\');
	}
	if (basename == NULL) {
		basename = filepath;
	} else {
		basename++; // Move past the directory separator
	}
	return basename;
}

/**
 * @brief Gets the file extension.
 *
 * Given 'filename.txt' as filename, this function will return 'txt'.
 *
 * @param fileName	The filename
 * @return The file extension.
 */
const C8* getFileExtension(const C8* fileName)
{
	// add +1 to not include the '.' itself
	const C8* extension = strrchr(fileName, '.');
	return extension ? (extension + 1) : nullptr;
}

/**
 * @brief Extracts the filename from the file string.
 *
 * Given 'filename.txt' as filename, this function will return 'filename'.
 *
 * @param name The full filename
 * @param fileName The extracted name (without extension)
 */
void extractFileName(const C8* name, C8* fileName)
{
	const C8* extension = strrchr(name, '.');
	if (extension) {
		memcpy(fileName, name, extension - name);
		fileName[extension - name] = '\0'; // Null-terminate the string
	} else {
		strcpy(fileName, name); // No extension, copy the entire string
	}
}

/**
 * @brief Checks if a string starts with another string.
 *
 * @param haystack The string to check
 * @param needle The start string
 *
 * @return True if `haystack` starts with `needle`, otherwise false.
 */
bool startsWith(const C8* haystack, const C8* needle)
{
	return (strncmp(haystack, needle, strlen(needle)) == 0);
}

/**
 * @brief Checks if a string ends with another string.
 *
 * @param haystack The string to check
 * @param needle The end string
 *
 * @return True if `haystack` ends with `needle`, otherwise false.
 */
bool endsWith(const C8* haystack, const C8* needle)
{
	const size_t haystack_len = strlen(haystack);
	const size_t needle_len   = strlen(needle);

	if (needle_len > haystack_len) return false;

	return (strncmp(haystack + haystack_len - needle_len, needle, needle_len) == 0);
}

/**
 * @brief Checks if a string starts with another string, ignoring case.
 *
 * @param haystack The string to check
 * @param needle The start string
 *
 * @return True if `haystack` starts with `needle` ignoring case, otherwise false.
 */
bool startsWithIgnoreCase(const C8* haystack, const C8* needle) {
	if (haystack == NULL || needle == NULL) return false;

	size_t i = 0;

	while (needle[i] != Null_) {
		if (haystack[i] == Null_)
			return false;

		if (tolower(haystack[i]) != tolower(needle[i]))
			return false;

		i++;
	}

	return true;
}

/**
 * @brief Determines if the given gregorian year is a leap year.
 *
 * @param GregorianYear The year to be checked
 * @return True if the year is a leap year, false otherwise
 */
bool isLeapYear(U16 GregorianYear)
{
	return (((GregorianYear % 100) != 0 && (GregorianYear % 4) == 0) || ((GregorianYear % 100) == 0 && (GregorianYear % 400) == 0));
}

/**
 * @brief Determines if the given date is valid.
 *
 * @param year The year component of the date (1-2999)
 * @param month The month component of the date (1-12)
 * @param day The day component of the date (1-31)
 *
 * @return True if the date is valid, false otherwise
 *
 */
#define isValidDate(year, month, day) (is_between(year, 1, 2999) && is_between(month, 1, 12) && is_between(day, 1, 31))

/**
 * @brief Determines if the given time is valid.
 *
 * @param hour The hour component of the time (0-23)
 * @param minute The minute component of the time (0-59)
 * @param second The second component of the time (0-59)
 *
 * @return True if the time is valid, false otherwise
 */
#define isValidTime(hour, minute, second) (is_between(hour, 0, 23) && is_between(minute, 0, 59) && is_between(second, 0, 59))

/**
 * @brief List of the short gregorian month names.
 */
const C8* monthNames[] PROGMEM = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * @brief List of the short gregorian week days.
 */
const C8* dayNames[] PROGMEM = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

/**
 * @brief Map "Jan"→0, "Feb"→1, …, "Dec"→11
 */
int monthStringToInt(const C8* mon) {
	for (size_t i = 0; i < count_of(monthNames); ++i) {
		if (strcasecmp(mon, monthNames[i]) == 0) {
			return i;
		}
	}
	return -1; // Default to -1 if the input does not match any month
}

/**
 * @brief Map 0 = Sunday, …, 6 = Saturday
 */
int dayOfWeek(int y, int m, int d) {
	static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
	if (m < 3) y -= 1;
	return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

/*
 * @brief Structure to hold a timestamp with year, month, day, hour, minute, second and milliseconds.
 *
 * This structure is used to represent a timestamp in a human-readable format.
 */
struct TimeStamp {
public:
	// Components of the timestamp
	U16 year;
	U8  month;
	U8  day;
	U8  hour;
	U8  minute;
	U8  second;
	U32 millisStamp;  // millis() when this timestamp was captured

	// default constructor initializes to zero values
	TimeStamp() : year(0), month(0), day(0), hour(0), minute(0), second(0), millisStamp(millis()) {}

	// constructor to initialize the timestamp
	TimeStamp(U16 y, U8 mo, U8 d, U8 h, U8 mi, U8 s) : year(y), month(mo), day(d), hour(h), minute(mi), second(s), millisStamp(millis()) {}

	/**
	 * @brief Sets the timestamp with the specified year, month, day, hour, minute, and second.
	 *
	 * This function sets the timestamp components and captures the current milliseconds.
	 *
	 * @param y The year to set (1-2999)
	 * @param mo The month to set (1-12)
	 * @param d The day to set (1-31)
	 * @param h The hour to set (0-23)
	 * @param mi The minute to set (0-59)
	 * @param s The second to set (0-59)
	 */
	bool set(U16 y, U8 mo, U8 d, U8 h, U8 mi, U8 s) {
		if (!isValidDate(y, mo, d) || !isValidTime(h, mi, s))
			return false; // Indicate failure due to invalid date/time

		year = y;
		month = mo;
		day = d;
		hour = h;
		minute = mi;
		second = s;
		millisStamp = millis(); // capture current milliseconds
		return true;
	}

	/**
	 * @brief Converts the year, month, day, hour, minute, and second components into a time_t value.
	 *
	 * This function converts the individual components of a timestamp into a time_t value,
	 * which represents the number of seconds since the epoch (January 1, 1970).
	 *
	 * @return The time_t value representing the timestamp.
	 */
	time_t toEpoch() const {
		struct tm tm_val = {0};
		tm_val.tm_year  = year - 1900;
		tm_val.tm_mon   = month - 1;
		tm_val.tm_mday  = day;
		tm_val.tm_hour  = hour;
		tm_val.tm_min   = minute;
		tm_val.tm_sec   = second;
		tm_val.tm_isdst = -1;
		return mktime(&tm_val);
	}

	/**
	 * @brief Converts a time_t value to a TimeStamp structure.
	 *
	 * This function takes a time_t value (representing seconds since the epoch)
	 * and converts it into a TimeStamp structure containing the year, month, day,
	 * hour, minute, second, and milliseconds.
	 *
	 * The milliseconds are captured using the millis() function at the time of conversion.
	 *
	 * @param epoch The time_t value to convert.
	 * @return A TimeStamp structure representing the given epoch time.
	 */
        static TimeStamp fromEpoch(time_t epoch) {
		struct tm *tm_ptr = localtime(&epoch);
		TimeStamp ts;
		ts.year   = tm_ptr->tm_year + 1900; // tm_year is years since 1900
		ts.month  = tm_ptr->tm_mon + 1;     // tm_mon is 0-11
		ts.day    = tm_ptr->tm_mday;
		ts.hour   = tm_ptr->tm_hour;
		ts.minute = tm_ptr->tm_min;
		ts.second = tm_ptr->tm_sec;
		ts.millisStamp = millis();
		return ts;
        }

	/**
	 * @brief Returns a new TimeStamp adjusted by the elapsed milliseconds since millisStamp.
	 *
	 * This function creates a new TimeStamp by converting the current timestamp to epoch time,
	 * adding the elapsed seconds since millisStamp, and then converting it back to a TimeStamp structure.
	 *
	 * @return The adjusted TimeStamp. If the time conversion was invalid, returns a default TimeStamp.
	 */
	TimeStamp adjusted() const {
		time_t epoch = toEpoch();
		U32 currentMillis = millis();
		if (epoch < 0 || millisStamp > currentMillis) return TimeStamp(); // Return default TimeStamp if conversion is invalid
		epoch += (currentMillis - millisStamp) / 1000; // Convert milliseconds to seconds
		TimeStamp ts = fromEpoch(epoch);
		ts.millisStamp = currentMillis;
		return ts;
	}

	/**
	 * @brief Checks if the timestamp is valid.
	 *
	 * This function checks if the year, month, day, hour, minute, and second components
	 * of the timestamp are within valid ranges.
	 *
	 * @return True if the timestamp is valid, false otherwise.
	 */
	bool isValid() const {
		return isValidDate(year, month, day) && isValidTime(hour, minute, second);
	}

	/**
	 * @brief Converts the timestamp to a human-readable string representation.
	 *
	 * This function formats the timestamp into a string in the format "YYYY-MM-DD HH:MM:SS".
	 *
	 * @return A String object containing the formatted timestamp.
	 */
	String toString() const {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d %02d:%02d:%02d",
		         year, month, day, hour, minute, second);
		return String(buffer);
	}
};

// The global timestamp variable to hold the current date and time
TimeStamp PU_DateTime;

/**
 * @brief Takes a string representation of date and time in the format "yyyy/mm/dd hh:mm:ss" and parses it into the numerical year, month, day, hour, minute, and second components.
 *
 * @param dateTimeStr	A string representing the date and time in the format "yyyy/mm/dd hh:mm:ss"
 * @param year		Reference to store the extracted year
 * @param month		Reference to store the extracted month
 * @param day		Reference to store the extracted day
 * @param hour		Reference to store the extracted hour
 * @param minute	Reference to store the extracted minute
 * @param second	Reference to store the extracted second
 *
 * @return True if the string was successfully parsed and the components extracted, false otherwise
 */
bool parseDateTime(const String& dateTimeStr, U16& year, U8& month, U8& day, U8& hour, U8& minute, U8& second) {

	// make sure all characters consist only of digits, spaces, / and :
	for (size_t i = 0; i < dateTimeStr.length(); i++) {
		char c = dateTimeStr[i];
		if (!isdigit(c) && c != ' ' && c != '/' && c != ':') {
			return false;
		}
	}

	// find the positions of the slashes, spaces and colons
	int slash1 = dateTimeStr.indexOf('/');
	int slash2 = dateTimeStr.indexOf('/', slash1 + 1);
	int space  = dateTimeStr.indexOf(' ', slash2 + 1);
	int colon1 = dateTimeStr.indexOf(':', space  + 1);
	int colon2 = dateTimeStr.indexOf(':', colon1 + 1);

	// ensure that year is exactly 4 digits
	if (slash1 != 4 || slash2 < 0 || space < 0 || colon1 < 0 || colon2 < 0) {
		return false;
	}

	// ensure that month and day are between 1-2 digits
	if (!is_between(slash2 - slash1, 2, 3) || !is_between(space - slash2, 2, 3)) {
		return false;
	}

	// ensure that and hour, minute and second are between 1-2 digits
	if (!is_between(colon1 - space, 2, 3) || !is_between(colon2 - colon1, 2, 3)) {
		return false;
	}

	// extract the year, month, day, hour, minute and second
	year	= dateTimeStr.substring(0, slash1).toInt();
	month	= dateTimeStr.substring(slash1 + 1, slash2).toInt();
	day	= dateTimeStr.substring(slash2 + 1, space).toInt();
	hour	= dateTimeStr.substring(space  + 1, colon1).toInt();
	minute	= dateTimeStr.substring(colon1 + 1, colon2).toInt();
	second	= dateTimeStr.substring(colon2 + 1).toInt();

	// perform date and time validation
	if (!isValidDate(year, month, day) || !isValidTime(hour, minute, second)) {
		return false;
	}

	return true;
}

/**
 * @brief Parses a date and time string into a TimeStamp structure.
 *
 * This function takes a date and time string in the format "yyyy/mm/dd hh:mm:ss" and parses it into a TimeStamp structure.
 * The current millis() value is captured at the time of parsing.
 *
 * @param dateTimeStr The date and time string to parse.
 * @param ts Reference to the TimeStamp structure to fill with parsed values.
 * @return True if the parsing was successful, false otherwise.
 */
bool parseDateTime(const String& dateTimeStr, TimeStamp& ts) {
	if (!parseDateTime(dateTimeStr, ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second)) {
		return false;
	}

	ts.millisStamp = millis(); // capture current millis

	return true;
}

// ---------------------------------------------------------------------

#include "LocalLib/RtcMemory.h"

// Global RTC memory instance for storing boot time and last known time
RtcMemory rtcMemory;

/**
 * @brief Formats the total seconds of uptime into a string representation.
 *
 * This function takes the total number of seconds of uptime and formats it into a string representation
 * in the format "HH:MM:SS" or "D days, HH:MM:SS" depending on the number of days.
 *
 * @param totalSeconds The total number of seconds of uptime.
 * @return A string representation of the uptime, formatted as "HH:MM:SS" or "D days, HH:MM:SS".
 */
String getFormattedUptime(U32 totalSeconds) {
	C8 uptimeString[32];

	const U32	seconds = (totalSeconds) % 60,
			minutes = (totalSeconds / 60) % 60,
			hours = (totalSeconds / (60 * 60)) % 24,
			days = (totalSeconds / (60 * 60 * 24));

	// Format the uptime as "HH:MM:SS"
	if (days == 0)	snprintf(uptimeString, sizeof(uptimeString),           PSTR("%02ld:%02ld:%02ld"),       hours, minutes, seconds);

	// Format the uptime as "days, HH:MM:SS"
	else		snprintf(uptimeString, sizeof(uptimeString), PSTR("%ld days, %02ld:%02ld:%02ld"), days, hours, minutes, seconds);

	return uptimeString;
}

/**
 * @brief Converts the given number of bytes into a human-readable string representation.
 *
 * The string representation ends with the unit suffix (B, KB, MB, GB).
 *
 * @param bytes The number of bytes to convert.
 * @return A string representation of the given number of bytes in human-readable format.
 */
String humanReadableBytes(U32 bytes) {
	const static C8* suffixes[] PROGMEM = {"B", "KB", "MB", "GB"};

	double ret = bytes;

	U8 i = 0;

	while (ret >= 1024 && i < count_of(suffixes)) {
		ret /= 1024.0;
		i++;
	}

	return String(ret, i > 1 ? 2 : 0) + " " + suffixes[i];
}

/**
 * @brief Sends a terse error text message (with no body for HEAD requests)
 *
 * @param request The request to send the error response to
 * @param code The HTTP status code to send
 * @param message The error message to send
 */
static void sendError(AsyncWebServerRequest* request, int code, const char* message)
{
	// For HEAD requests, send empty body
	request->send(code, "text/plain", request->method() != HTTP_HEAD ? message : "");
}

constexpr size_t TAIL_SIZE = 1024;

/**
 * Reads the last `tailSize` bytes from flash into `outBuf`.
 *
 * @param outBuf   Pointer to a buffer of at least `tailSize` bytes.
 * @param tailSize Number of bytes to read from the end of flash (must be >0 and ≤ TAIL_SIZE).
 * @return         Number of bytes that was read (== tailSize) or 0 on any error.
 */
size_t readFlashTail(uint8_t* outBuf, size_t tailSize) {
	// Validate input arguments and basic constraints
	if (tailSize == 0 || tailSize > TAIL_SIZE || outBuf == nullptr) {
		return 0;
	}

	// Determine flash size and validate capacity to ensure that request fits
	const uint32_t flashSize = ESP.getFlashChipSize();
	if (tailSize > flashSize) {
		return 0;
	}

	// Calculate start address to read from
	const uint32_t startAddr = flashSize - tailSize;	// Desired start address for reading
	const uint32_t alignedStartAddr = startAddr & ~0x03;	// Aligned start address down to the nearest 4-byte boundary
	const size_t offset = startAddr - alignedStartAddr;	// How many bytes to skip in the read buffer
	const size_t requiredReadLen = tailSize + offset;	// Total bytes we need to read

	// Calculate total bytes to read safely (rounded up to multiple of 4)
	const size_t readSize    = (requiredReadLen + 3) & ~0x03; // Round up to 4-byte multiple
	const size_t maxReadSize = (flashSize - alignedStartAddr) & ~0x03; // Max safe aligned read

	// Ensure we don't read past flash end
	if (readSize > maxReadSize) {
		return 0;
	}

	// Allocate temporary buffer for reading
	uint8_t* tempBuf = static_cast<uint8_t*>(malloc(readSize));
	if (tempBuf == nullptr) {
		return 0;
	}

	// Perform flash read and check results
	if (!ESP.flashRead(alignedStartAddr, tempBuf, readSize)) {
		free(tempBuf);
		return 0;
	}

	// Copy requested bytes and clean up
	memcpy(outBuf, tempBuf + offset, tailSize);
	free(tempBuf);

	return tailSize;
}

// ---------------------------------------------------------------------

#undef os_printf
#define os_printf(...)

#ifndef BUILD_USERNAME
#define BUILD_USERNAME "Unkown"
#endif

#ifndef BUILD_HOSTNAME
#define BUILD_HOSTNAME "Unkown"
#endif

#ifdef __CODEVISIONAVR__
#error This project should be compiled with GCC, not CodeVisionAVR!
#else
#define delay_ms delay
#define delay_us delayMicroseconds
#endif

#if defined(NDEBUG) && (defined(DebugOnSerial) || defined(DebugTools))
#error Debugging is enabled in release mode!
#endif

#ifdef DebugTools
#warning Debugging Tools are enabled!
#define IsDebugEnabled() (On_)
#else
#define IsDebugEnabled() (Off_)
#endif

#if defined(DebugOnSerial)
#include "debug.h"
#undef os_printf
#define DEBUG
#define DEBUG_SKETCH
#define DEBUG_DNSSERVER
#define DEBUG_ESP_WIFI
#define DEBUG_ESP_OTA
#define DEBUG_ESP_ASYNC_TCP
#define DEBUG_NTPClient
#define DEBUG_ESP_PORT Serial
#define DEBUG_UPDATER DEBUG_ESP_PORT
#define DEBUG_PRINTF DEBUG_ESP_PORT.printf
#define DEBUG_PRINTLN DEBUG_ESP_PORT.println
#define DEBUG_PRINT DEBUG_ESP_PORT.print
#define os_printf DEBUG_PRINTF
#warning Debugging on Serial is enabled!
#endif

#if defined(ShellOnSerial) || defined(DebugOnSerial)
#warning UART service is disabled for the shell/debug!
void	Serial_Write(C8 ch)	{ }
int	Serial_Available()	{ return 0; }
char	Serial_Read()		{ return 0; }
#else
#define Serial_Write(ch)	Serial.write(ch)
#define Serial_Available()	Serial.available()
#define Serial_Read()		Serial.read()
#endif
