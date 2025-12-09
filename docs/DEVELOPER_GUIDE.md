# PU850 ESP8266 Developer Guide

## Table of Contents

1. [Development Environment](#development-environment)
2. [Project Architecture](#project-architecture)
3. [Building and Flashing](#building-and-flashing)
4. [Code Structure](#code-structure)
5. [UART Protocol](#uart-protocol)
6. [LocalLib Components](#locallib-components)
7. [Customization Guide](#customization-guide)
8. [Debugging](#debugging)
9. [Testing](#testing)
10. [Contributing](#contributing)

## Development Environment

### Prerequisites

#### Required Software
- **Arduino CLI** v1.0.0 or later
- **Git** for version control
- **Text Editor/IDE**: VS Code, Arduino IDE, or similar

#### Required Libraries
All dependencies are defined in `sketch.yaml`:

```yaml
libraries:
  - ESP_EEPROM (2.2.1)
  - ESP Telnet (2.2.3)
  - ESPAsyncTCP (2.0.0)
  - ESP Async WebServer (3.9.2)
  - NTPClient (3.2.1)
  - ArduinoJson (7.4.2)
  - Arduino_JSON (0.2.0)
```

#### Platform
```yaml
platform: esp8266:esp8266 (3.1.2)
platform_index_url: http://arduino.esp8266.com/stable/package_esp8266com_index.json
```

### Environment Setup

#### Linux/macOS

```bash
# Install Arduino CLI
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Add to PATH
export PATH=$PATH:$HOME/bin

# Initialize configuration
arduino-cli config init

# Add ESP8266 board manager URL
arduino-cli config add board_manager.additional_urls \
  https://arduino.esp8266.com/stable/package_esp8266com_index.json

# Update package index
arduino-cli core update-index

# Install ESP8266 core
arduino-cli core install esp8266:esp8266@3.1.2

# Install required libraries
arduino-cli lib install "ESP_EEPROM@2.2.1"
arduino-cli lib install "ESP Telnet@2.2.3"
arduino-cli lib install "ESPAsyncTCP@2.0.0"
arduino-cli lib install "ESP Async WebServer@3.9.2"
arduino-cli lib install "NTPClient@3.2.1"
arduino-cli lib install "ArduinoJson@7.4.2"
arduino-cli lib install "Arduino_JSON@0.2.0"
```

#### Windows

```cmd
REM Install Arduino CLI - download from arduino.cc
REM Add to PATH environment variable

REM Initialize and configure (same commands as Linux)
arduino-cli config init
arduino-cli config add board_manager.additional_urls ^
  https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266@3.1.2

REM Install libraries (same as Linux)
```

## Project Architecture

### High-Level Overview

```
┌─────────────────┐
│  Web Browser    │ ←→ HTTP/WebSocket
│  Mobile App     │
└────────┬────────┘
         │
         ↓
┌─────────────────────────────┐
│      ESP8266 Firmware       │
│  ┌───────────────────────┐  │
│  │  Web Server           │  │
│  │  - HTTP API           │  │
│  │  - WebSocket          │  │
│  │  - Static Files       │  │
│  └───────────────────────┘  │
│  ┌───────────────────────┐  │
│  │  Network Services     │  │
│  │  - WiFi Management    │  │
│  │  - SSDP/mDNS/MNDP    │  │
│  │  - Telnet Shell       │  │
│  └───────────────────────┘  │
│  ┌───────────────────────┐  │
│  │  UART Protocol        │  │
│  │  - Command/Response   │  │
│  │  - Bulk Data Transfer │  │
│  │  - File Operations    │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ UART (115200)
              ↓
┌─────────────────────────────┐
│     PU850 Main Unit         │
│  - Weight Sensor            │
│  - Display/LCD              │
│  - Keypad                   │
│  - Storage                  │
└─────────────────────────────┘
```

### Memory Layout

```
Flash Memory (1MB):
├── Bootloader         (~4KB)
├── Sketch/Program     (~700-800KB)
├── File System        (Optional, not used)
└── EEPROM Emulation   (~4KB)
    └── Settings Storage

RAM (80KB):
├── System Stack       (~4KB)
├── Heap               (~40KB usable)
│   ├── Network Buffers
│   ├── UART Buffers
│   ├── WebSocket Clients
│   └── Dynamic Allocations
└── Global Variables   (~36KB)
```

## Building and Flashing

### Build Process

#### Standard Build
```bash
# Linux/macOS
./build.sh

# Windows
build.cmd
```

#### Build with Debug Features
```bash
# Enable debug tools (web endpoints)
./build.sh --debug-tools

# Enable serial debug output
./build.sh --debug-on-serial

# Enable shell on serial (disables UART to PU850)
./build.sh --shell-on-serial

# Combine multiple options
./build.sh --debug-tools --debug-on-serial
```

### Build Output

The build process generates:
```
Build/
├── ASA0002E.ino.bin       # Compressed firmware (gzipped)
├── ASA0002E.ino.bin.md5   # MD5 checksums
├── ASA0002E.ino.elf       # ELF executable
├── ASA0002E.ino.map       # Memory map
└── sketch/                # Intermediate build files
```

### Flashing Methods

#### Method 1: USB/Serial (First-time)

```bash
# Using esptool.py
esptool.py --port /dev/ttyUSB0 --baud 115200 write_flash 0x00000 Build/ASA0002E.ino.bin

# Or using Arduino CLI
arduino-cli upload --port /dev/ttyUSB0 --fqbn esp8266:esp8266:generic
```

#### Method 2: OTA Update

```bash
# Using curl
curl -F "update=@Build/ASA0002E.ino.bin" http://[device-ip]/update

# Or via web browser
# Navigate to http://[device-ip]/update
```

#### Method 3: Network Upload (esptool)

```bash
# For devices with OTA bootloader
python3 espota.py -i [device-ip] -f Build/ASA0002E.ino.bin
```

## Code Structure

### Main Files

#### ASA0002E.ino
The main firmware file containing:
- `setup()`: Initialization routine
  - Serial/UART setup
  - WiFi configuration
  - Web server initialization
  - Service startup
- `loop()`: Main event loop
  - UART processing
  - Network service updates
  - Periodic tasks
- HTTP endpoint handlers
- WebSocket event handlers
- WiFi event callbacks

#### ASA0002E.h
Main header file with:
- Core data structures
- Global variables
- Function prototypes
- Helper functions
- Utility macros

#### defines.h
Common definitions:
- Type definitions (U8, U16, U32, etc.)
- Protocol constants
- Shared enums and structures

### Project Organization

```
ASA0002E.ino
├── Includes
│   ├── Arduino.h
│   ├── ESP8266WiFi.h
│   ├── ESP_EEPROM.h
│   ├── ESPAsyncTCP.h
│   ├── ESPAsyncWebServer.h
│   └── Third-party libraries
├── LocalLib (Custom implementations)
│   ├── AsyncOTA.*
│   ├── AsyncWebServer.*
│   ├── Authentication.*
│   ├── Commands.*
│   ├── CrashHandler.*
│   ├── MNDP.*
│   ├── Sessions.*
│   ├── Shell.*
│   ├── SSDP.*
│   ├── Telnet.*
│   └── UART.*
└── Core Functions
    ├── WiFi_Setup()
    ├── Settings_Read()
    ├── Settings_Write()
    ├── setup()
    └── loop()
```

## UART Protocol

### Protocol Overview

The ESP8266 communicates with the PU850 main unit using a binary protocol over UART:

- **Baud Rate**: 115200
- **Data Format**: 8N1
- **Flow Control**: None
- **Buffer Size**: 1024 bytes
- **Frame Format**: SOH + [Data] + EOT

### Frame Structure

```
┌─────┬────────┬────────┬──────────┬─────┐
│ SOH │ Prefix │ Suffix │ Payload  │ EOT │
└─────┴────────┴────────┴──────────┴─────┘
  1B      1B       1B      0-N bytes   1B

SOH = 0x01 (Start of Header)
EOT = 0x04 (End of Transmission)
```

### Message Types

#### 1. Command (ESP → PU)
```c
// Request weight value
SendCommand(ESP_Prefix_Request, ESP_Suffix_Weigh, Once_);

// Tare scale
SendCommand(ESP_Prefix_Request, ESP_Suffix_Tare, 0);
```

#### 2. Response (PU → ESP)
```c
// Format: Prefix + Suffix + Response Code (16-bit)
void onResponseReceived(U8 Prefix, U8 Suffix, U16 Response) {
    switch(Response) {
        case result_Success:      // Operation succeeded
        case result_Fail:         // Operation failed
        case result_Not_Found:    // Resource not found
        case result_Unauthorised: // Access denied
        // ...
    }
}
```

#### 3. Data Transfer (PU → ESP)
```c
// Numeric value
void ReadEPfromPU_Num(U8 Suffix);

// String value  
void ReadEPfromPU_Str(U8 Suffix);

// Bulk data (files, receipts)
// Handled automatically by state machine
```

### Protocol State Machine

```c
// States defined in UART.h
#define WaitforSOH_     0   // Waiting for start of header
#define WaitforPrefix_  1   // Waiting for prefix byte
#define WaitforSuffix_  2   // Waiting for suffix byte
#define Waitfor4Num_    3   // Waiting for 4-byte number
#define WaitforBulk_    4   // Waiting for bulk data
#define WaitforFile_    5   // Waiting for file data
#define WaitforFourth_  6   // Waiting for 4th byte
#define WaitforThird_   7   // Waiting for 3rd byte
#define WaitforSecond_  8   // Waiting for 2nd byte
#define WaitforFirst_   9   // Waiting for 1st byte
#define WaitforEOT_     10  // Waiting for end of transmission
```

### Common Operations

#### Reading Weight
```c
// Request current weight
Request_GiveWeight();

// Wait for response
// Result stored in eWeightValue (global)
if (eWeightReady && eWeightValue != UnDefinedNum_) {
    // Weight is valid
    S32 weight = eWeightValue;
}
```

#### Sending Commands
```c
// Send a tare command
bool success = Request_Tare(0);
if (success == OK_) {
    // Tare initiated successfully
}
```

#### File Downloads
```c
// Request file from PU850
U8 result = Request_GiveFile(filename, offset, length);

if (result == result_Success) {
    // File transfer started
    // Data arrives in BulkBuffer
    // Read with ReadFromBulk()
}
```

## LocalLib Components

### AsyncOTA.cpp/h
**Purpose**: Handles Over-The-Air firmware updates

**Key Features**:
- MD5 verification of uploaded firmware
- Progress tracking
- Automatic reboot after update
- Error handling and rollback

**Usage**:
```cpp
AsyncOTA.begin(server);  // Initialize OTA handler

// In loop():
if (AsyncOTA.rebootRequired()) {
    // Prepare for reboot
    ESP.restart();
}
```

### AsyncWebServer.cpp/h
**Purpose**: Web server and WebSocket configuration

**Key Components**:
- HTTP server instance
- WebSocket endpoint `/ws`
- CORS middleware
- Static content

**WebSocket Protocol**:
```javascript
// Client-side example
const ws = new WebSocket('ws://device-ip/ws');

ws.onmessage = (event) => {
    // Messages from ESP:
    // "status:1"          - Status update
    // "123"               - Weight value
    // "off"               - Weight unavailable
    // "msg:5,overflow"    - System message
    // "beep:2"            - Beep notification
};

ws.send('weight');   // Request weight update
ws.send('datetime'); // Request date/time
```

### Authentication.cpp/h
**Purpose**: Session management and authentication

**Key Classes**:

1. **LoginHandler**: Processes login requests
```cpp
// Handles POST /login
// Extracts username/password
// Creates session token
```

2. **AuthenticationMiddleware**: Extracts credentials
```cpp
// Checks Authorization header
// Extracts Bearer tokens
// Validates session cookies
```

3. **AuthorizationMiddleware**: Validates sessions
```cpp
// Verifies session tokens
// Checks access levels
// Enforces security policies
```

**Session Structure**:
```cpp
typedef struct {
    char authToken[16];      // Raw token bytes
    byte accessLevel;         // User access level
    uint32_t lastActive;     // Last activity timestamp
    IPAddress lastIP;        // Client IP address
    bool isLoggedIn;         // Login status
} session;
```

### Commands.cpp/h
**Purpose**: Shell command processing

**Available Commands**:
- `help` - Show command list
- `info` - System information
- `reset` - Reboot device
- `wifi` - WiFi status
- `heap` - Memory usage
- `uptime` - System uptime
- Custom commands can be added

### CrashHandler.cpp/h
**Purpose**: Detect and report system crashes

**Features**:
- Watchdog timer handling
- Exception detection
- Crash log storage
- Reset reason analysis

### MNDP.cpp/h
**Purpose**: MikroTik Neighbor Discovery Protocol

**Features**:
- Device announcement on multicast
- TLV (Type-Length-Value) encoding
- Periodic broadcasts
- Network topology discovery

**TLV Types**:
```cpp
TLV_TYPE_MAC_ADDRESS = 1
TLV_TYPE_IDENTITY    = 5
TLV_TYPE_VERSION     = 7
TLV_TYPE_PLATFORM    = 8
TLV_TYPE_UPTIME      = 10
TLV_TYPE_IPV4_ADDR   = 17
```

### Sessions.cpp/h
**Purpose**: User session management

**Functions**:
```cpp
// Initialize session system
void initSessions();

// Create new session
int createSession(IPAddress ip);

// Validate session token
int validateSession(const char* token, IPAddress ip);

// Destroy session
void destroySession(int sessionId);

// Cleanup expired sessions
void cleanupSessions();
```

### Shell.cpp/h
**Purpose**: Interactive command-line interface

**Features**:
- ANSI escape code support
- Command history
- Tab completion
- Color output
- Cursor movement

**ANSI Codes**:
```cpp
#define KEY_ESCAPE 0x1B
#define CTRL_C     0x03
#define KEY_BKSP   0x08
#define KEY_TAB    0x09
#define KEY_ENTER  0x0D
#define ARR_UP     0x41
#define ARR_DOWN   0x42
```

### SSDP.cpp/h
**Purpose**: Simple Service Discovery Protocol

**Features**:
- UPnP device discovery
- SSDP announcement
- Service description XML
- M-SEARCH responses

**Device Description**:
```xml
<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion>
    <major>1</major>
    <minor>0</minor>
  </specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>
    <friendlyName>PU850</friendlyName>
    <manufacturer>Faravary Pand Caspian</manufacturer>
    <modelName>PU850-ESP8266</modelName>
    <serialNumber>...</serialNumber>
  </device>
</root>
```

### Telnet.cpp/h
**Purpose**: Telnet server for remote shell access

**Features**:
- Single concurrent connection
- IAC protocol handling
- Shell integration
- ANSI terminal support

**Protocol Handling**:
```cpp
// Telnet commands
#define IAC  0xFF  // Interpret As Command
#define DONT 0xFE  // Don't perform option
#define DO   0xFD  // Do perform option
#define WONT 0xFC  // Won't perform option
#define WILL 0xFB  // Will perform option
```

### UART.cpp/h
**Purpose**: Serial communication with PU850

**Key Functions**:

#### Initialization
```cpp
bool InitializeUARTService();
```

#### Sending Commands
```cpp
bool SendCommand(U8 Prefix, U8 Suffix, U32 Value);
bool SendResponseCode_ToPU(U8 Prefix, U8 Suffix, U16 code);
bool SendESPStatus_ToPU(U8 status, bool force);
```

#### Reading Values
```cpp
bool ReadEPfromPU_Num(U8 Suffix);
bool ReadEPfromPU_Str(U8 Suffix);
bool ReadEPfromPU_DateTime();
```

#### Writing Values
```cpp
bool WriteEPtoPU_Num(U8 Suffix);
bool WriteEPtoPU_Str(U8 Suffix);
bool WriteEPtoPU_DateTime();
```

#### Bulk Data Operations
```cpp
void BulkReset(U8 state);
U8 ReadFromBulk();
U32 GetBulkRemain();
bool BulkDetected;
bool BulkOverflow;
```

#### Request Functions
```cpp
bool Request_Tare(U8 preset);
S32 Request_GiveWeight();
bool Request_ShowHideWeight(U8 state);
U8 Request_GivePower();
U8 Request_GiveReceipt(U32 id);
U8 Request_GiveFile(const C8* filename, U32 offset, U32 length);
```

## Customization Guide

### Adding New HTTP Endpoints

```cpp
// In setup() function:
server->on("/custom/endpoint", HTTP_GET, [&](AsyncWebServerRequest *request) {
    // Handle query parameters
    if (request->hasArg("param")) {
        String value = request->arg("param");
        // Process parameter
    }
    
    // Send response
    request->send(200, "text/plain", "Response text");
});
```

### Adding WebSocket Commands

```cpp
// In onWsReceivedCommand():
if (strcasecmp(data, "custom_command") == 0) {
    // Handle custom command
    client->text("server:custom_response");
}
```

### Adding UART Commands

```cpp
// 1. Define command in Temp/ESP_Master_Common.h
#define ESP_Suffix_CustomCommand  0xXX

// 2. Implement request function
bool Request_CustomOperation(U32 param) {
    return SendCommand(ESP_Prefix_Request, ESP_Suffix_CustomCommand, param);
}

// 3. Handle response in onResponseReceived()
case ESP_Suffix_CustomCommand:
    if (Response == result_Success) {
        // Handle success
    }
    break;
```

### Adding Shell Commands

```cpp
// In Shell.cpp, add to command processing:
if (CompStr(command, "mycmd") == Yes_) {
    String_ToShell("Custom command executed\r\n");
    // Perform operation
    return;
}
```

### Custom Middleware

```cpp
class CustomMiddleware : public AsyncMiddleware {
public:
    void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {
        // Pre-processing
        
        next();  // Call next middleware/handler
        
        // Post-processing
    }
};

// In setup():
CustomMiddleware customMiddleware;
server->addMiddleware(&customMiddleware);
```

## Debugging

### Debug Build Options

```bash
# Enable debug tools (adds debug endpoints)
./build.sh --debug-tools

# Enable serial debug output
./build.sh --debug-on-serial

# Enable shell on serial (for development without PU850)
./build.sh --shell-on-serial
```

### Debug Endpoints

When built with `--debug-tools`:

```
GET /debug/info      - Debug buffer information
GET /debug/data      - View debug buffer contents
POST /debug/clear    - Clear debug buffer
GET /eProcessState   - UART state machine status
GET /eResponseStatus - Last response status
GET /crash           - Trigger intentional crash (for testing)
```

### Debug Macros

```cpp
#ifdef DebugTools
    // Debug code here
    WriteToDebug("Debug message\n");
#endif

#ifdef DebugOnSerial
    Serial.println("Debug output");
#endif
```

### Memory Debugging

```cpp
// Check free heap
uint32_t freeHeap = ESP.getFreeHeap();
Serial.printf("Free heap: %u bytes\n", freeHeap);

// Check heap fragmentation
uint32_t maxBlock = ESP.getMaxFreeBlockSize();
Serial.printf("Max free block: %u bytes\n", maxBlock);

// Check stack usage
uint32_t freeStack = ESP.getFreeContStack();
Serial.printf("Free stack: %u bytes\n", freeStack);
```

### UART Debugging

```cpp
// Enable UART debug logging
#define UART_DEBUG

// In UART.cpp, log protocol events:
void SerialPortReceiveProcess() {
    #ifdef UART_DEBUG
    Serial.printf("State: %d, Prefix: 0x%02X, Suffix: 0x%02X\n", 
                  eProcessState, ePrefix, eSuffix);
    #endif
    // ...
}
```

### Network Debugging

```cpp
// WiFi events
void wifi_handle_event_cb(System_Event_t *evt) {
    os_printf("WiFi Event: %d\n", evt->event);
    // Handle event
}

// HTTP requests
server->onNotFound([](AsyncWebServerRequest *request) {
    Serial.printf("404: %s %s\n", 
                  request->methodToString(), 
                  request->url().c_str());
});
```

## Testing

### Unit Testing

Create test functions for critical components:

```cpp
// Test CRC32 calculation
void test_CRC32() {
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t crc = calculateCRC32(data, sizeof(data));
    assert(crc == 0x89ABCDEF);  // Expected value
}

// Test hostname validation
void test_HostnameValidation() {
    assert(isValidHostName("valid-host") == true);
    assert(isValidHostName("Invalid Host") == false);
    assert(isValidHostName("-invalid") == false);
}
```

### Integration Testing

Test complete workflows:

```cpp
void test_WeightReading() {
    // 1. Request weight
    Request_GiveWeight();
    
    // 2. Wait for response
    delay(100);
    
    // 3. Verify result
    assert(eWeightReady == true);
    assert(eWeightValue != UnDefinedNum_);
}
```

### Network Testing

Use curl for API testing:

```bash
# Test weight endpoint
curl http://device-ip/weight/get

# Test tare command
curl -X POST http://device-ip/action/tare

# Test WebSocket
curl --include \
     --no-buffer \
     --header "Connection: Upgrade" \
     --header "Upgrade: websocket" \
     --header "Host: device-ip" \
     --header "Sec-WebSocket-Key: SGVsbG8sIHdvcmxkIQ==" \
     --header "Sec-WebSocket-Version: 13" \
     http://device-ip/ws
```

### Load Testing

Test with multiple concurrent connections:

```python
import asyncio
import websockets

async def test_client():
    async with websockets.connect('ws://device-ip/ws') as ws:
        await ws.send('weight')
        response = await ws.recv()
        print(f"Received: {response}")

# Run multiple clients
async def main():
    tasks = [test_client() for _ in range(10)]
    await asyncio.gather(*tasks)

asyncio.run(main())
```

### Hardware Testing

Test with actual PU850:

1. **Power-on test**: Verify boot sequence
2. **UART communication**: Check command/response
3. **Weight accuracy**: Compare readings
4. **Network stability**: Long-running connections
5. **OTA update**: Firmware update process
6. **Recovery**: Reset and error handling

## Contributing

### Code Style Guidelines

1. **Naming Conventions**:
   - Variables: `camelCase` or `snake_case`
   - Functions: `CamelCase` or `snake_case`
   - Constants: `UPPER_CASE` or `PascalCase`
   - Macros: `UPPER_CASE`

2. **Formatting**:
   - Indent with tabs (width 8)
   - Braces on same line (K&R style)
   - Max line length: 120 characters

3. **Comments**:
   ```cpp
   /**
    * @brief Brief function description
    *
    * Detailed description of what the function does.
    *
    * @param param1 Description of parameter 1
    * @param param2 Description of parameter 2
    * @return Description of return value
    */
   ```

4. **Error Handling**:
   ```cpp
   // Check return values
   if (!functionCall()) {
       // Handle error
       return false;
   }
   
   // Use result codes
   U8 result = operation();
   switch (result) {
       case result_Success: break;
       case result_Fail: return false;
   }
   ```

### Pull Request Process

1. Fork the repository
2. Create feature branch: `git checkout -b feature/my-feature`
3. Make changes and test thoroughly
4. Commit with descriptive message
5. Push to your fork
6. Create pull request with:
   - Clear description
   - Test results
   - Screenshots (if UI changes)

### Testing Requirements

Before submitting PR:

- [ ] Code compiles without warnings
- [ ] Tested on actual hardware
- [ ] No memory leaks detected
- [ ] Network functions work
- [ ] UART communication verified
- [ ] Documentation updated
- [ ] No security vulnerabilities

### Documentation

Update documentation when:
- Adding new features
- Changing API endpoints
- Modifying protocol
- Updating dependencies
- Fixing bugs that affect usage

---

## Additional Resources

- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [Arduino CLI](https://arduino.github.io/arduino-cli/)
- [ESP8266 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)

For questions or support, contact: David@Refoua.me
