#pragma once
#include "../ASA0002E.h"

// Constants to define the parameter types
typedef enum {
	TYPE_STRING = 0x0,
	TYPE_INT    = 0x1,
} ParameterType;

#define FLAG_READONLY 0x80

// enum ParameterType type;

// Struct to store device parameters
typedef const struct {
	const C8* name;
	const U8  type;
	// const void* value;
	U8 (*get)(C8*);
	U8 (*set)(const C8*);
} DeviceParameter;

// Define a structure to hold the command-function mapping
typedef const struct {
	const C8* command;
	void (*function)(const C8*);
} CommandEntry;

U8 getDeviceParameterIdByName(const C8* paramName);
#define AssignResult(args,c_str) (strncpy(args, c_str, CMD_LEN - strlen(command)))
