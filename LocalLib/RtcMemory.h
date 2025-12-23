#pragma once

/**
 * @file RtcMemory.h
 * @brief RTC user memory management for persistent storage across reboots
 *
 * This module provides a class for storing and retrieving data from the ESP8266's
 * RTC user memory, which persists across deep sleep and soft resets.
 *
 * Memory Layout (RTC user memory = 512 bytes / 128 blocks of 4 bytes):
 * |<------system data (256 bytes)------->|<-----------------user data (512 bytes)--------------->|
 * OTA takes the first 128 bytes of the USER area.
 * The offset is measured in blocks of 4 bytes and can range from 0 to 127 blocks.
 * Data stored in the first 32 blocks will be lost after performing an OTA update.
 *
 * Data is protected with a CRC32 checksum to ensure integrity.
 */

#include "../ASA0002E.h"

// RTC user memory offset (in 4-byte blocks). Must be ≥32 to avoid OTA overlap.
#define RTC_USER_DATA_ADDR 32

class RtcMemory {
public:
	// Error codes for RTC operations
	enum class ErrorCode : uint8_t {
		OK = 0,
		ReadFail,
		CrcMismatch,
		WriteFail
	};

	RtcMemory() : error_(ErrorCode::OK) {
		clear();
	}

	// Erase all stored data (sets to zero)
	void clear() {
		rtcData_ = DataBlock();  // Value-initialization instead of memset
		error_ = ErrorCode::OK;
	}

	// Read from RTC memory; validates CRC
	bool read() {
		clear();
		if (!ESP.rtcUserMemoryRead(RTC_USER_DATA_ADDR, reinterpret_cast<uint32_t*>(&rtcData_), sizeof(rtcData_))) {
			error_ = ErrorCode::ReadFail;
			return false;
		}

		// Compute CRC over data after the crc32 field
		uint32_t crc = calculateCRC32(
			reinterpret_cast<const uint8_t*>(&rtcData_) + sizeof(rtcData_.crc32),
			sizeof(rtcData_) - sizeof(rtcData_.crc32)
		);

		if (crc != rtcData_.crc32) {
			error_ = ErrorCode::CrcMismatch;
			clear();
			return false;
		}

		error_ = ErrorCode::OK;
		return true;
	}

	// Write current data to RTC memory, updating CRC
	bool write() {
		// Compute CRC over data after the crc32 field
		rtcData_.crc32 = calculateCRC32(
			reinterpret_cast<const uint8_t*>(&rtcData_) + sizeof(rtcData_.crc32),
			sizeof(rtcData_) - sizeof(rtcData_.crc32)
		);

		if (!ESP.rtcUserMemoryWrite(RTC_USER_DATA_ADDR, reinterpret_cast<uint32_t*>(&rtcData_), sizeof(rtcData_))) {
			error_ = ErrorCode::WriteFail;
			return false;
		}

		error_ = ErrorCode::OK;
		return true;
	}

	// Initialize both boot and lastKnown timestamps at startup
	bool initAtBoot(const TimeStamp& ts) {
		rtcData_.boot = ts;
		rtcData_.lastKnown = ts;
		return write();
	}

	// Convenience method to initialize with time_t
	bool initAtBoot(time_t epochTime) {
		return initAtBoot(TimeStamp::fromEpoch(epochTime));
	}

	// Update only the lastKnown timestamp
	bool updateTime(const TimeStamp& ts) {
		rtcData_.lastKnown = ts;
		return write();
	}

	// Convenience method to update with time_t
	bool updateTime(time_t epochTime) {
		return updateTime(TimeStamp::fromEpoch(epochTime));
	}

	// Initialize if not valid, otherwise update - keeps calling code DRY
	bool initOrUpdate(const TimeStamp& ts) {
		if (!rtcData_.boot.isValid()) {
			return initAtBoot(ts);
		}
		return updateTime(ts);
	}

	// Compute current local time based on stored lastKnown + elapsed millis
	void getCurrentDateTime(struct tm &out) const {
		time_t currentEpoch = getCurrentEpoch();
		localtime_r(&currentEpoch, &out);
	}

	// Get current time as epoch
	// Note: Uses unsigned arithmetic for millis() delta which correctly handles
	// wraparound (every ~49 days) as long as time is updated more frequently
	time_t getCurrentEpoch() const {
		time_t baseEpoch = rtcData_.lastKnown.toEpoch();
		uint32_t currentMs = millis();
		uint32_t deltaMs = currentMs - rtcData_.lastKnown.millisStamp;
		return baseEpoch + (deltaMs / 1000);
	}

	// Calculate uptime in seconds since boot
	uint32_t getUptimeSeconds() const {
		time_t bootEpoch = rtcData_.boot.toEpoch();
		time_t currentEpoch = getCurrentEpoch();
		// Ensure we don't return negative uptime if timestamps are invalid
		if (currentEpoch < bootEpoch) return 0;
		return static_cast<uint32_t>(currentEpoch - bootEpoch);
	}

	// Accessors
	const TimeStamp& lastKnown() const { return rtcData_.lastKnown; }
	const TimeStamp& boot() const { return rtcData_.boot; }
	ErrorCode error() const { return error_; }

	// Get error as string for debugging
	const char* errorString() const {
		switch (error_) {
			case ErrorCode::OK:          return "OK";
			case ErrorCode::ReadFail:    return "Read Failed";
			case ErrorCode::CrcMismatch: return "CRC Mismatch";
			case ErrorCode::WriteFail:   return "Write Failed";
			default:                     return "Unknown Error";
		}
	}

private:
	// Raw block stored in RTC RAM
	struct DataBlock {
		uint32_t  crc32;           // CRC32 checksum (must be first)
		TimeStamp lastKnown;       // Last known accurate time
		TimeStamp boot;            // Time when device first booted
	} rtcData_;

	ErrorCode error_;
};
