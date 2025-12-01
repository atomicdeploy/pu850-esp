#pragma once

/**
 * @file defines.h
 * @brief Common definitions and configuration includes for the PU850 ESP firmware
 *
 * This file includes shared type definitions, constants, and protocol definitions
 * used across the firmware. It conditionally includes the build-time generated
 * local header when USE_LOCALH is defined.
 */

#include <Arduino.h>

#ifdef USE_LOCALH
#include "~local.h"
#endif

// Include shared definitions from the Temp directory
// These files contain type definitions, constants, and protocol specifications
// shared with the main PU unit firmware.
#include "Temp/CommonDefine.h"
#include "Temp/Always.h"
#include "Temp/ESP_Master_Common.h"
#include "Temp/ChartIndex.h"
