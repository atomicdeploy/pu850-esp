#include "../ASA0002E.h"

#include "Sessions.h"

void initSessions() {
	// os_printf("Initializing %d Sessions...\n", count_of(sessions));

	for (size_t i = 0; i < count_of(sessions); i++) {
		sessions[i].isLoggedIn = false;
		sessions[i].accessLevel = UnknownLevel_;
		sessions[i].lastActive = 0;
	}
}

bool addSession(const char* authToken, uint8_t accessLevel, IPAddress lastIP) {
	bool slotAvailable = false;
	int slotIndex = -1;

	uint32_t oldestTime = millis();

	for (size_t i = 0; i < count_of(sessions); i++)
	{
		if (!sessions[i].isLoggedIn) {
			slotIndex = i;
			slotAvailable = true;
			break;
		}

		if (sessions[i].lastActive < oldestTime) {
			slotIndex = i;
			oldestTime = sessions[i].lastActive;
		}
	}

	if (slotAvailable)
	{
		memcpy(sessions[slotIndex].authToken, authToken, sizeof(sessions[slotIndex].authToken));
		sessions[slotIndex].accessLevel = accessLevel;
		sessions[slotIndex].lastIP = lastIP;
		sessions[slotIndex].lastActive = millis();
		sessions[slotIndex].isLoggedIn = true;
	}

	return slotAvailable;
}

void destroySession(const char* authToken) {
	for (size_t i = 0; i < count_of(sessions); i++)
	{
		if (memcmp(sessions[i].authToken, authToken, sizeof(sessions[i].authToken)) != 0)
			continue;

		if (sessions[i].isLoggedIn) {
			sessions[i].isLoggedIn = false;
			break;
		}
	}
}

int validateSession(const char* authToken, IPAddress lastIP) {
	for (size_t i = 0; i < count_of(sessions); i++)
	{
		if (memcmp(sessions[i].authToken, authToken, sizeof(sessions[i].authToken)) != 0)
			continue;

		sessions[i].lastIP = lastIP;
		sessions[i].lastActive = millis();

		if (sessions[i].isLoggedIn)
			return i;
	}

	return -1;
}

void SessionCommand(uint8_t command, size_t sessionIndex) {

}
