# LocalLib Components Reference

This document provides detailed information about each component in the LocalLib directory.

## Table of Contents

1. [AsyncOTA](#asyncota)
2. [AsyncWebServer](#asyncwebserver)
3. [Authentication](#authentication)
4. [Commands](#commands)
5. [CrashHandler](#crashhandler)
6. [MNDP](#mndp)
7. [Sessions](#sessions)
8. [Shell](#shell)
9. [SSDP](#ssdp)
10. [Telnet](#telnet)
11. [UART](#uart)
12. [Public](#public)

---

## AsyncOTA

**Files**: `AsyncOTA.cpp`, `AsyncOTA.h`

### Purpose
Handles Over-The-Air (OTA) firmware updates through the web interface.

### Key Features
- Web-based firmware upload
- MD5 checksum verification
- Progress tracking
- Automatic reboot after update
- Error handling and recovery

### API

#### Initialization
```cpp
AsyncOTA.begin(server);
```

#### Check for Reboot
```cpp
if (AsyncOTA.rebootRequired()) {
    // Perform cleanup
    ESP.restart();
}
```

### Update Process
1. Navigate to `http://device-ip/update`
2. Select firmware binary file
3. Upload starts automatically
4. Progress displayed as percentage
5. Verification with MD5 checksum
6. Automatic reboot on success

### Security Considerations
- No authentication (to be implemented)
- Verify firmware before upload
- Keep backup of working firmware
- Use compressed binaries (gzipped)

### Error Handling
- Upload timeout: 30 seconds
- Max size: Free sketch space
- Invalid firmware: Rejected
- MD5 mismatch: Rejected
- Write error: Aborted

---

## AsyncWebServer

**Files**: `AsyncWebServer.cpp`, `AsyncWebServer.h`

### Purpose
Configures the asynchronous web server and WebSocket endpoint.

### Key Components

#### HTTP Server
```cpp
U16 HTTP_Port = 80;
AsyncWebServer *server = new AsyncWebServer(HTTP_Port);
```

#### WebSocket
```cpp
const int MAX_WS_CLIENTS = 10;
AsyncWebSocket ws("/ws");
```

### WebSocket Protocol

#### Client → Server
- `weight` - Request weight update
- `datetime` - Request date/time
- Custom commands as implemented

#### Server → Client
- `[number]` - Weight value
- `off` - Weight unavailable
- `status:[n]` - Status update
- `msg:[icon],[text]` - Message
- Various state updates

### Configuration
- Max WebSocket clients: 10
- HTTP port: 80
- Auto-ping enabled
- Message buffer: 512 bytes

### Usage Example
```cpp
// Initialize
initWebSocket();

// Send to all clients
ws.textAll("message");

// Send to specific client
client->text("response");

// Broadcast to all
ws.printfAll("format:%d", value);
```

---

## Authentication

**Files**: `Authentication.cpp`, `Authentication.h`

### Purpose
Provides session-based authentication and authorization middleware.

### Components

#### 1. LoginHandler
Handles login requests at `/login` endpoint.

```cpp
class LoginHandler : public AsyncWebHandler {
    bool canHandle(AsyncWebServerRequest *request);
    void handleRequest(AsyncWebServerRequest *request);
};
```

**Features**:
- Username/password validation
- Session token generation
- Cookie management

#### 2. AuthenticationMiddleware
Extracts and validates credentials from requests.

```cpp
class AuthenticationMiddleware : public AsyncMiddleware {
    void run(AsyncWebServerRequest *request, ArMiddlewareNext next);
    void collectCreds(AsyncWebServerRequest *request);
    int checkSession(AsyncWebServerRequest *request);
};
```

**Credential Sources**:
- Authorization header (Basic, Bearer)
- Cookies (TOKEN=...)
- Query parameters (?token=...)
- POST body (username/password)

#### 3. AuthorizationMiddleware
Enforces access control based on session validation.

```cpp
class AuthorizationMiddleware : public AsyncMiddleware {
    void run(AsyncWebServerRequest *request, ArMiddlewareNext next);
};
```

### Authentication Flow
```
1. Extract credentials → collectCreds()
2. Validate session → checkSession()
3. Store in request attributes
4. Pass to next middleware/handler
```

### Access Levels
```cpp
enum AccessLevel {
    UnknownLevel_ = 0,  // No access
    GuestLevel_   = 1,  // Read-only
    UserLevel_    = 2,  // Standard operations
    AdminLevel_   = 3,  // Configuration
    OwnerLevel_   = 4   // Full control
};
```

### Session Token Format
- Length: 32 characters (16 bytes hex)
- Generation: Cryptographically secure random
- Storage: Raw bytes in session structure
- Transmission: Hexadecimal string

---

## Commands

**Files**: `Commands.cpp`, `Commands.h`

### Purpose
Implements shell command processing for the Telnet interface.

### Available Commands

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show help | `help` |
| `info` | System info | `info` |
| `reset` | Reboot device | `reset` |
| `wifi` | WiFi status | `wifi` |
| `heap` | Memory usage | `heap` |
| `uptime` | System uptime | `uptime` |
| `clear` | Clear screen | `clear` |
| `exit` | Close connection | `exit` |

### Adding Custom Commands

```cpp
// In Shell.cpp
if (CompStr(command, "mycommand") == Yes_) {
    String_ToShell("Command executed\r\n");
    // Implementation
    return;
}
```

### Command Processing Flow
```
Input → Parse → Match Command → Execute → Output
```

---

## CrashHandler

**Files**: `CrashHandler.cpp`, `CrashHandler.h`

### Purpose
Detects system crashes and provides diagnostics.

### Features
- Watchdog timer handling
- Exception detection
- Reset reason analysis
- Crash logging (if enabled)

### Reset Reasons
```cpp
REASON_DEFAULT_RST       // Power-on/Hard reset
REASON_WDT_RST           // Hardware watchdog
REASON_EXCEPTION_RST     // Fatal exception
REASON_SOFT_WDT_RST      // Software watchdog
REASON_SOFT_RESTART      // Software restart
REASON_DEEP_SLEEP_AWAKE  // Wake from deep sleep
REASON_EXT_SYS_RST       // External system reset
```

### Usage
```cpp
// Check reset reason
const rst_info* resetInfo = system_get_rst_info();

switch (resetInfo->reason) {
    case REASON_EXCEPTION_RST:
        // Handle exception
        break;
    // ...
}
```

### Exception Information
- EPC1, EPC2, EPC3: Exception program counters
- EXCVADDR: Exception virtual address
- DEPC: Double exception program counter

---

## MNDP

**Files**: `MNDP.cpp`, `MNDP.h`

### Purpose
Implements MikroTik Neighbor Discovery Protocol for device discovery.

### Features
- Multicast announcements
- TLV (Type-Length-Value) encoding
- Periodic broadcasts
- Network topology discovery

### TLV Types
```cpp
TLV_TYPE_MAC_ADDRESS = 1   // Device MAC
TLV_TYPE_IDENTITY    = 5   // Hostname
TLV_TYPE_VERSION     = 7   // Firmware version
TLV_TYPE_PLATFORM    = 8   // Hardware platform
TLV_TYPE_UPTIME      = 10  // System uptime
TLV_TYPE_IPV4_ADDR   = 17  // IPv4 address
```

### Configuration
```cpp
const IPAddress mndpAddress(224, 0, 0, 88);
const unsigned int mndpPort = 5678;
```

### Broadcast Interval
- Default: 60 seconds
- Multicast group: 224.0.0.88
- Port: 5678 UDP

### Usage
```cpp
// Initialize
setupMNDP();

// Send announcement
sendMNDP();

// Called periodically in loop()
```

### Discovery Tools
- Winbox (MikroTik)
- The Dude (MikroTik)
- Custom MNDP browsers

---

## Sessions

**Files**: `Sessions.cpp`, `Sessions.h`

### Purpose
Manages user authentication sessions and tokens.

### Session Structure
```cpp
typedef struct {
    char authToken[16];      // 128-bit token
    byte accessLevel;        // 0-4
    uint32_t lastActive;     // Timestamp
    IPAddress lastIP;        // Client IP
    bool isLoggedIn;         // Status
} session;
```

### Configuration
```cpp
const int MAX_SESSIONS = 8;
const int TOKEN_LENGTH = 32;  // Hex string
```

### API

#### Create Session
```cpp
int createSession(IPAddress ip) {
    // Find free slot
    // Generate token
    // Initialize session
    // Return session ID
}
```

#### Validate Session
```cpp
int validateSession(const char* token, IPAddress ip) {
    // Find session by token
    // Verify IP match
    // Check timeout
    // Update lastActive
    // Return session ID or -1
}
```

#### Destroy Session
```cpp
void destroySession(int sessionId) {
    // Clear session data
    // Mark as available
}
```

#### Cleanup Expired
```cpp
void cleanupSessions() {
    // Iterate sessions
    // Check timeout
    // Destroy expired
}
```

### Session Lifecycle
```
Create → Active → Timeout/Logout → Cleanup
```

### Security Features
- IP address binding
- Timeout after inactivity
- Secure random tokens
- Limited concurrent sessions

---

## Shell

**Files**: `Shell.cpp`, `Shell.h`

### Purpose
Provides interactive command-line interface for Telnet.

### Features
- ANSI terminal support
- Command history
- Line editing
- Cursor movement
- Color output

### Key Functions

#### Command Processing
```cpp
void ShellService(C8 ch);
```

#### Output Functions
```cpp
void String_ToShell(const C8* str);
void Number_ToShell(S32 N);
void NewLine_ToShell(U8 n);
void HexNumber_ToShell(U32 num);
```

#### Input Handling
```cpp
// Parse number from string
bool NumberFromString(const C8* str, S32* num);

// Compare strings
U8 CompStr(const C8* str1, const C8* str2);
```

### ANSI Escape Codes
```cpp
#define KEY_ESCAPE 0x1B
#define ARR_UP     0x41
#define ARR_DOWN   0x42
#define ARR_RIGHT  0x43
#define ARR_LEFT   0x44
```

### Command Line Editing
- **Backspace**: Delete character
- **Arrow Left/Right**: Move cursor
- **Arrow Up/Down**: Command history (future)
- **Ctrl+C**: Cancel command
- **Ctrl+L**: Clear screen

### Color Support
```cpp
// ANSI color codes
ANSI_BRIGHT_GREEN   // Success
ANSI_BRIGHT_YELLOW  // Warning
ANSI_BRIGHT_RED     // Error
ANSI_BRIGHT_CYAN    // Info
```

---

## SSDP

**Files**: `SSDP.cpp`, `SSDP.h`, `SSDPDevice.cpp`, `SSDPDevice.h`

### Purpose
Implements Simple Service Discovery Protocol (UPnP).

### Features
- Device announcement
- M-SEARCH responses
- XML device description
- Service discovery

### Configuration
```cpp
SSDP.setName(hostname);
SSDP.setSerialNumber(serialNumber);
SSDP.setModelName("PU850-ESP8266");
SSDP.setManufacturer("Faravary Pand Caspian");
```

### Device Description
```xml
<root>
  <device>
    <deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>
    <friendlyName>PU850</friendlyName>
    <manufacturer>Faravary Pand Caspian</manufacturer>
    <modelName>PU850-ESP8266</modelName>
    <serialNumber>123456</serialNumber>
    <UDN>uuid:...</UDN>
  </device>
</root>
```

### SSDP Messages

#### NOTIFY (Announcement)
```
NOTIFY * HTTP/1.1
HOST: 239.255.255.250:1900
NT: upnp:rootdevice
NTS: ssdp:alive
USN: uuid:device-uuid::upnp:rootdevice
LOCATION: http://device-ip/description.xml
```

#### M-SEARCH (Discovery)
```
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
ST: upnp:rootdevice
```

### Discovery Tools
- Windows Network Discovery
- UPnP Control Points
- Network scanners

---

## Telnet

**Files**: `Telnet.cpp`, `Telnet.h`

### Purpose
Provides Telnet server for remote shell access.

### Features
- Single connection support
- IAC protocol handling
- Shell integration
- ANSI terminal emulation

### Configuration
```cpp
U16 Telnet_Port = 23;
ESPTelnet telnet;
```

### IAC Protocol
```cpp
#define IAC  0xFF  // Interpret As Command
#define DONT 0xFE  // Don't perform option
#define DO   0xFD  // Do perform option
#define WONT 0xFC  // Won't perform option
#define WILL 0xFB  // Will perform option
```

### Connection Flow
```
1. Client connects → Port 23
2. Telnet negotiation → IAC commands
3. Enter shell → Command prompt
4. Execute commands → Responses
5. Disconnect → Cleanup
```

### Usage
```cpp
// Setup
Telnet_Setup();

// Service (call in loop)
Telnet_Service();

// Send output
telnet.print("message\n");
telnet.printf("value: %d\n", value);
```

### Security Note
- No authentication (plain text)
- Single connection only
- Local network use recommended
- Consider SSH for production

---

## UART

**Files**: `UART.cpp`, `UART.h`

### Purpose
Implements custom binary protocol for communication with PU850 main unit.

### Protocol Specification

#### Frame Format
```
SOH (0x01)
Prefix (1 byte) - Message type
Suffix (1 byte) - Message subtype
Payload (0-N bytes) - Data
EOT (0x04)
```

#### State Machine
```cpp
#define WaitforSOH_     0   // Start of header
#define WaitforPrefix_  1   // Message type
#define WaitforSuffix_  2   // Message subtype
#define Waitfor4Num_    3   // 4-byte number
#define WaitforBulk_    4   // Bulk data
#define WaitforFile_    5   // File data
#define WaitforEOT_     10  // End of transmission
```

### Key Functions

#### Protocol Processing
```cpp
void SerialPortReceiveProcess();
bool InitializeUARTService();
```

#### Command Sending
```cpp
bool SendCommand(U8 Prefix, U8 Suffix, U32 Value);
bool SendResponseCode_ToPU(U8 Prefix, U8 Suffix, U16 code);
```

#### Data Reading
```cpp
bool ReadEPfromPU_Num(U8 Suffix);
bool ReadEPfromPU_Str(U8 Suffix);
bool ReadEPfromPU_DateTime();
```

#### Data Writing
```cpp
bool WriteEPtoPU_Num(U8 Suffix);
bool WriteEPtoPU_Str(U8 Suffix);
bool WriteEPtoPU_DateTime();
```

#### Request Functions
```cpp
S32 Request_GiveWeight();
bool Request_Tare(U8 preset);
U8 Request_GivePower();
bool Request_ShowHideWeight(U8 state);
U8 Request_GiveReceipt(U32 id);
U8 Request_GiveFile(const C8* filename, U32 offset, U32 length);
```

### Bulk Data Handling
```cpp
// Buffer management
void BulkReset(U8 state);
U8 ReadFromBulk();
U32 GetBulkRemain();

// Status flags
bool BulkDetected;
bool BulkOverflow;
U32 BulkTotalRead;
U32 FinalDataSize;
```

### Error Codes
```cpp
#define err_SOH_NotDetect                    11
#define err_EOT_NotDetect                    12
#define err_Prefix_NotDetect                 13
#define err_OutOfBulkSize                    17
#define err_Time_Out                         100
```

### Configuration
```cpp
U32 BAUD_RATE = 115200;
#define UART_BUFFER_SIZE 1024
#define validTimeOut_ 1000L
```

---

## Public

**Files**: `Public.cpp`, `Public.h`

### Purpose
Provides boolean type compatibility macros.

### Content
```cpp
#include <stdbool.h>

#ifndef bool
#define bool boolean
#endif

#ifndef true
#define true  1
#endif

#ifndef false
#define false 0
#endif
```

### Purpose
Ensures boolean type compatibility across different build environments and legacy Arduino code.

---

## Integration Example

### Complete Setup Flow
```cpp
void setup() {
    // 1. Initialize UART
    Serial.begin(BAUD_RATE);
    InitializeUARTService();
    
    // 2. Setup WiFi
    WiFi_Setup();
    
    // 3. Initialize Web Server
    server = new AsyncWebServer(80);
    initWebSocket();
    
    // 4. Setup Authentication
    server->addMiddleware(&authMiddleware);
    server->addMiddleware(&authzMiddleware);
    server->addHandler(&loginHandler);
    
    // 5. Setup OTA
    AsyncOTA.begin(server);
    
    // 6. Setup Discovery
    initSSDP();
    setupMNDP();
    MDNS.begin(hostname);
    
    // 7. Setup Telnet
    Telnet_Setup();
    
    // 8. Start Server
    server->begin();
}

void loop() {
    // Process UART
    SerialPortReceiveProcess();
    
    // Update Services
    Telnet_Service();
    MDNS.update();
    sendMNDP();
    SSDP_Service();
    
    // Cleanup
    ws.cleanupClients();
    cleanupSessions();
}
```

---

For more information:
- [Developer Guide](DEVELOPER_GUIDE.md)
- [Architecture](ARCHITECTURE.md)
- [API Reference](API_REFERENCE.md)

Contact: David@Refoua.me
