#pragma once

/**
 * @file Sessions.h
 * @brief Session management structures and functions
 *
 * Manages user authentication sessions for the web interface,
 * including token-based authentication and access level tracking.
 */

const int MAX_SESSIONS = 8;

const int TOKEN_LENGTH = 32; // 16 bytes, 32 characters

typedef struct {
	char authToken[TOKEN_LENGTH / 2]; // store as raw bytes
	byte accessLevel;
	uint32_t lastActive;
	IPAddress lastIP;
	bool isLoggedIn;
} session;

session sessions[MAX_SESSIONS];

void SessionCommand(uint8_t command, size_t sessionIndex);
