# TODO Items Status

This document tracks the status of TODO items found in the codebase.

## Completed TODOs ✅

### 1. NTP Client Configuration
- **Location**: `ASA0002E.ino:116`
- **Status**: ✅ IMPLEMENTED
- **Description**: Configure NTP client with proper server and parameters
- **Implementation**: Configured to use `pool.ntp.org` with UTC timezone (offset 0) and 60-second update interval
- **Commit**: Implemented in current PR

### 2. NTP Time Offset
- **Location**: `ASA0002E.ino:577`
- **Status**: ✅ IMPLEMENTED (removed redundant call)
- **Description**: Time offset is now set in the NTPClient constructor
- **Implementation**: Offset (0 for UTC) is configured in the constructor, so the redundant `setTimeOffset()` call was removed
- **Commit**: Implemented in current PR

### 3. SSDP CORS Header
- **Location**: `LocalLib/SSDP.cpp:225`
- **Status**: ✅ IMPLEMENTED
- **Description**: Add Access-Control-Allow-Origin header for SSDP endpoint
- **Implementation**: Added CORS header with value "*" to allow cross-origin requests to `/description.xml`
- **Commit**: Implemented in current PR

## Pending TODOs 📋

### 4. Static IP for AP Mode
- **Location**: `ASA0002E.ino:149`
- **Status**: ⚠️ PARTIALLY DONE
- **Description**: Set static IP for AP mode
- **Current State**: Static IP is already configured (192.168.4.1), but TODO comment remains
- **Notes**: The implementation is actually complete - `WiFi.softAPConfig()` sets the IP to 192.168.4.1. The TODO can be removed if this is the desired configuration.

### 5. Static IP for Station Mode
- **Location**: `ASA0002E.ino:155`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Set static IP for station mode
- **Current State**: Code is commented out, waiting for flashSettings structure enhancement
- **Dependencies**: Requires adding fields to `flashSettings` structure (see TODO #9)
- **Implementation Plan**: 
  1. Add fields to flashSettings: `dhcp_enabled`, `static_ip[4]`, `gateway[4]`, `subnet[4]`, `dns[4]`
  2. Update settings save/load functions
  3. Uncomment and integrate the WiFi.config() code

### 6. Static IP Settings in Flash Structure
- **Location**: `ASA0002E.h:60`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Add static IP settings and port settings to flashSettings structure
- **Current State**: Structure only contains WiFi mode, hostname, and credentials
- **Implementation Plan**:
  ```c
  struct flashSettings {
      U32 crc32;
      U8 net_flags;
      C8 hostname[FCSTS_ + 1];
      C8 ap_ssid[FCSTS_ + 1];
      C8 ap_password[FCSTS_ + 1];
      C8 sta_ssid[FCSTS_ + 1];
      C8 sta_password[FCSTS_ + 1];
      
      // Static IP settings
      U8 dhcp_enabled;
      U8 static_ip[4];
      U8 gateway[4];
      U8 subnet[4];
      U8 dns[4];
      
      // Port settings
      U16 web_port;
      U16 telnet_port;
  }
  ```

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

### 9. Settings Error Notification
- **Location**: `ASA0002E.ino:2434`
- **Status**: ❌ NOT IMPLEMENTED
- **Description**: Send error notifications to PU when settings have errors
- **Current State**: Switch statement is commented out
- **Implementation Plan**:
  1. Verify Request_SendMessage_ToPU() function signature
  2. Uncomment and test error cases
  3. Ensure SendCommand() with ESP_Suffix_RestoreBackup works correctly
- **Error Codes**:
  - `1`: No data (ESP Restored)
  - `2`: Invalid data (ESP Corrupted)
  - `3`: Request by user (ESP RestByUsr)

### 10. Test Code Removal
- **Location**: `ASA0002E.ino:2185`
- **Status**: ❌ PENDING REMOVAL
- **Description**: Remove "client:helloWorld" test code
- **Current State**: Test handler is still present
- **Notes**: Marked for removal but kept for now. Safe to remove when no longer needed for testing.

### 11. MAX_SESSIONS Write Support
- **Location**: `LocalLib/UART.cpp:925`
- **Status**: ❌ NOT POSSIBLE (by design)
- **Description**: Allow setting MAX_SESSIONS from UART
- **Current State**: Code is commented out
- **Blocker**: MAX_SESSIONS is defined as `const int` in Sessions.h
- **Notes**: Would require architectural change to make MAX_SESSIONS mutable or use dynamic allocation

### 12. MAX_SESSIONS Read
- **Location**: `LocalLib/UART.cpp:1025`
- **Status**: ✅ ALREADY IMPLEMENTED
- **Description**: Return MAX_SESSIONS value
- **Current State**: Implementation exists and returns MAX_SESSIONS
- **Notes**: TODO comment can be safely removed as this is complete

### 13. File Transfer Implementation
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

### 14. UART.h Variable Documentation
- **Location**: `LocalLib/UART.h:48`
- **Status**: ⚠️ UNCLEAR
- **Description**: Empty TODO comment above WiFi/network variables
- **Current State**: Variables are defined and used
- **Notes**: May need additional documentation or the TODO can be removed

## Summary

- **Total TODOs Found**: 14
- **Implemented**: 3 ✅
- **Partially Done**: 2 ⚠️
- **Not Implemented**: 9 ❌

## Priority Recommendations

### High Priority
1. **Static IP Configuration** (TODOs #5, #6) - Important for network deployment
2. **Settings Error Notification** (TODO #9) - Important for user feedback

### Medium Priority
3. **RTC Memory Implementation** (TODO #7) - Useful for time persistence
4. **Crash Handler** (TODO #8) - Useful for debugging production issues
5. **File Transfer** (TODO #13) - Depends on use case requirements

### Low Priority / Cleanup
6. **Test Code Removal** (TODO #10) - Safe to remove anytime
7. **Documentation** (TODO #14) - Can be addressed during code review
8. **MAX_SESSIONS** (TODOs #11, #12) - #12 is done, #11 requires design decision

## Notes

- This document should be updated as TODOs are resolved or new ones are discovered
- Each TODO should be evaluated for priority and dependencies before implementation
- Some TODOs may require architectural decisions before implementation
