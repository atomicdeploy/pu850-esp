# TODO Items Status

This document tracks the status of TODO items found in the codebase.

## Completed TODOs ✅

### 1. NTP Client Configuration
- **Location**: `ASA0002E.ino:116`
- **Status**: ✅ IMPLEMENTED
- **Description**: Configure NTP client with proper server and parameters
- **Implementation**: Configured to use `pool.ntp.org` with UTC timezone (offset 0) and 60-second update interval
- **Commit**: Implemented in current PR

### 2. SSDP CORS Header
- **Location**: `LocalLib/SSDP.cpp:225`
- **Status**: ✅ IMPLEMENTED
- **Description**: Add Access-Control-Allow-Origin header for SSDP endpoint
- **Implementation**: Added CORS header with value "*" to allow cross-origin requests to `/description.xml`
- **Commit**: Implemented in current PR

### 3. Static IP for AP Mode
- **Location**: `ASA0002E.ino:149`
- **Status**: ✅ ALREADY IMPLEMENTED
- **Description**: Set static IP for AP mode
- **Current State**: Static IP is configured to 192.168.4.1 via `WiFi.softAPConfig()`
- **Notes**: Implementation was already complete

### 4. Static IP for Station Mode
- **Location**: `ASA0002E.ino:155`
- **Status**: ✅ IMPLEMENTED
- **Description**: Set static IP for station mode
- **Implementation**: 
  - Added `dhcp_enabled`, `static_ip[4]`, `gateway[4]`, `subnet[4]`, `dns[4]` fields to flashSettings
  - Implemented `WiFi.config()` call when DHCP is disabled
  - Defaults: DHCP enabled, static IP 192.168.1.100, gateway 192.168.1.1, subnet 255.255.255.0, DNS 8.8.8.8
- **Commit**: Implemented in current PR

### 5. Static IP Settings in Flash Structure
- **Location**: `ASA0002E.h:60`
- **Status**: ✅ IMPLEMENTED
- **Description**: Add static IP settings and port settings to flashSettings structure
- **Implementation**: Added the following fields:
  - `U8 dhcp_enabled` - DHCP enable flag
  - `U8 static_ip[4]` - Static IP address
  - `U8 gateway[4]` - Gateway address
  - `U8 subnet[4]` - Subnet mask
  - `U8 dns[4]` - DNS server
  - `U16 web_port` - Web server port (default 80)
  - `U16 telnet_port` - Telnet port (default 23)
- **Commit**: Implemented in current PR

### 6. Settings Error Notification
- **Location**: `ASA0002E.ino:2434`
- **Status**: ✅ IMPLEMENTED (partial)
- **Description**: Send error notifications to PU when settings have errors
- **Implementation**: Implemented switch statement to send appropriate messages:
  - Case 1: "ESP Restored" (No data)
  - Case 2: "ESP Corrupted" (Invalid data)
  - Case 3: "ESP RestByUsr" (Request by user)
- **Notes**: `SendCommand(ESP_Prefix_Request, ESP_Suffix_RestoreBackup, settingsError)` remains commented due to bug on PU850 side
- **Commit**: Implemented in current PR

## Pending TODOs 📋

### 7. RTC Memory for Date/Time
- **Location**: `ASA0002E.ino:1975`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Implement RTC memory for storing device date/time across deep sleep
- **Current State**: Commented example code showing API usage
- **Implementation Plan**:
  1. Define RTC memory structure for date/time
  2. Implement write to RTC on date/time updates
  3. Implement read from RTC on boot/wake
  4. Handle OTA limitations (first 32 blocks are overwritten)
- **Notes**: 
  - RTC memory is 512 bytes total (user area)
  - OTA uses first 128 bytes
  - Data must be 4-byte aligned
  - Useful for maintaining time across deep sleep cycles

### 8. Custom Crash Callback
- **Location**: `LocalLib/CrashHandler.cpp:10`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Define custom crash callback for ESP8266 exception handling
- **Current State**: Function skeleton exists but is commented out
- **Implementation Plan**:
  1. Determine where to store crash data (EEPROM, RTC memory, or flash)
  2. Implement crash data storage (timestamps, reset info, stack traces)
  3. Uncomment and complete the callback function
  4. Implement crash data retrieval and reporting
- **Notes**: Must be quick and concise to execute before hardware WDT kicks in

### 9. Test Code Removal
- **Location**: `ASA0002E.ino:2185`
- **Status**: ❌ PENDING REMOVAL
- **Description**: Remove "client:helloWorld" test code
- **Current State**: Test handler is still present
- **Notes**: Marked for removal but kept for now. Safe to remove when no longer needed for testing.

### 10. MAX_SESSIONS Write Support
- **Location**: `LocalLib/UART.cpp:925`
- **Status**: ❌ NOT POSSIBLE (by design)
- **Description**: Allow setting MAX_SESSIONS from UART
- **Current State**: Code is commented out
- **Blocker**: MAX_SESSIONS is defined as `const int` in Sessions.h
- **Notes**: Would require architectural change to make MAX_SESSIONS mutable or use dynamic allocation

### 11. MAX_SESSIONS Read
- **Location**: `LocalLib/UART.cpp:1025`
- **Status**: ✅ ALREADY IMPLEMENTED
- **Description**: Return MAX_SESSIONS value
- **Current State**: Implementation exists and returns MAX_SESSIONS
- **Notes**: TODO comment can be safely removed as this is complete

### 12. File Transfer Implementation
- **Location**: `LocalLib/UART.cpp:2127`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Implement file transfer from ESP to PU over UART
- **Current State**: Empty case in switch statement
- **Implementation Plan**:
  1. Design file transfer protocol
  2. Implement file reading from SPIFFS/LittleFS
  3. Implement chunked transmission over UART
  4. Add progress tracking and error handling
- **Dependencies**: Requires file system to be set up with files to transfer

### 13. UART.h Variable Documentation
- **Location**: `LocalLib/UART.h:48`
- **Status**: ⚠️ UNCLEAR
- **Description**: Empty TODO comment above WiFi/network variables
- **Current State**: Variables are defined and used
- **Notes**: May need additional documentation or the TODO can be removed

## Summary

- **Total TODOs Found**: 13
- **Implemented**: 6 ✅
- **Already Complete**: 2 ✅
- **Not Implemented**: 4 ❌
- **Not Possible**: 1 ❌

## Priority Recommendations

### High Priority
1. **RTC Memory Implementation** (TODO #7) - Useful for time persistence across deep sleep
2. **Crash Handler** (TODO #8) - Useful for debugging production issues

### Medium Priority
3. **File Transfer** (TODO #12) - Depends on use case requirements

### Low Priority / Cleanup
4. **Test Code Removal** (TODO #9) - Safe to remove anytime
5. **Documentation** (TODO #13) - Can be addressed during code review
6. **MAX_SESSIONS Write** (TODO #10) - Requires design decision (make const mutable or use dynamic allocation)

## Notes

- This document should be updated as TODOs are resolved or new ones are discovered
- Each TODO should be evaluated for priority and dependencies before implementation
- Some TODOs may require architectural decisions before implementation
