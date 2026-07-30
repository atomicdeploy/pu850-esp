# PU850 ESP8266 Architecture

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Software Architecture](#software-architecture)
4. [Communication Protocols](#communication-protocols)
5. [Data Flow](#data-flow)
6. [Memory Management](#memory-management)
7. [Network Stack](#network-stack)
8. [Security Model](#security-model)
9. [State Management](#state-management)
10. [Performance Considerations](#performance-considerations)

## System Overview

The PU850 ESP8266 firmware acts as a WiFi-to-UART bridge, enabling network connectivity for the PU850 electronic scale. The system follows a layered architecture with clear separation of concerns.

```
┌─────────────────────────────────────────────────────────┐
│                    User Interface Layer                  │
│  Web Browser │ Mobile App │ Desktop App │ API Clients   │
└─────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────┐
│                   Network Protocol Layer                 │
│    HTTP/HTTPS │ WebSocket │ Telnet │ Discovery          │
└─────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────┐
│                  Application Logic Layer                 │
│  Request Handlers │ Session Manager │ State Machine     │
└─────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────┐
│                    UART Protocol Layer                   │
│   Frame Parser │ Command Encoder │ Response Decoder     │
└─────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────┐
│                       PU850 Hardware                     │
│     Display │ Keypad │ Sensor │ Storage │ Processor     │
└─────────────────────────────────────────────────────────┘
```

### Design Principles

1. **Event-Driven**: Asynchronous processing for network and UART
2. **Non-Blocking**: All I/O operations are non-blocking
3. **Resource-Constrained**: Optimized for limited RAM/Flash
4. **Modular**: Clear separation between components
5. **Extensible**: Easy to add new features and protocols

## Hardware Architecture

### ESP8266 Module

```
┌─────────────────────────────────────────┐
│         ESP8266 SoC                     │
│  ┌──────────────────────────────────┐  │
│  │  Tensilica L106 32-bit RISC      │  │
│  │  Clock: 80/160 MHz                │  │
│  │  RAM: 80 KB user-available       │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │  Flash: 1 MB                      │  │
│  │  ├─ Bootloader (~4KB)            │  │
│  │  ├─ Firmware (~800KB)            │  │
│  │  └─ EEPROM (~4KB)                │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │  WiFi 802.11 b/g/n               │  │
│  │  2.4 GHz                          │  │
│  └──────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### Pin Configuration

| Pin | Function | Direction | Description |
|-----|----------|-----------|-------------|
| GPIO 0 | Boot Mode | Input | Pull-up for normal boot |
| GPIO 1 (TX) | UART TX | Output | Serial transmit to PU850 |
| GPIO 2 | Status LED | Output | Optional status indicator |
| GPIO 3 (RX) | UART RX | Input | Serial receive from PU850 |
| GPIO 5 | MRST | Output | Hardware reset for PU850 |
| GPIO 16 | GWP | Output | Status signal to PU850 |
| CHIP_EN | Enable | Input | Chip enable (pull-up) |
| RST | Reset | Input | Hardware reset (pull-up) |

### Physical Connections

```
ESP8266                          PU850 Main Unit
┌─────────┐                     ┌──────────────┐
│         │                     │              │
│  TX  ───┼─────────────────────┤ RX           │
│         │                     │              │
│  RX  ───┼─────────────────────┤ TX           │
│         │                     │              │
│ MRST ───┼─────────────────────┤ RESET        │
│         │                     │              │
│ GWP  ───┼─────────────────────┤ STATUS_IN    │
│         │                     │              │
│ GND  ───┼─────────────────────┤ GND          │
│         │                     │              │
│ 3.3V ───┼─────────────────────┤ 3.3V         │
│         │                     │              │
└─────────┘                     └──────────────┘
```

### Power Requirements

- **Supply Voltage**: 3.3V (±10%)
- **Operating Current**:
  - Active (WiFi TX): ~170mA peak
  - Active (WiFi RX): ~60mA
  - Modem sleep: ~15mA
  - Light sleep: ~0.9mA
  - Deep sleep: ~20µA

### Clock Configuration

- **CPU Clock**: 80 MHz (configurable to 160 MHz)
- **Flash Clock**: 40 MHz
- **Flash Mode**: QIO (Quad I/O)
- **Crystal**: 26 MHz

## Software Architecture

### Layered Architecture

```
┌─────────────────────────────────────────────┐
│         Presentation Layer                  │
│  ┌──────────────┐  ┌──────────────┐        │
│  │ HTTP Handler │  │ WebSocket    │        │
│  │              │  │ Handler      │        │
│  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────────┐
│         Business Logic Layer                │
│  ┌──────────────┐  ┌──────────────┐        │
│  │ Request      │  │ Session      │        │
│  │ Processor    │  │ Manager      │        │
│  └──────────────┘  └──────────────┘        │
│  ┌──────────────┐  ┌──────────────┐        │
│  │ Command      │  │ State        │        │
│  │ Dispatcher   │  │ Manager      │        │
│  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────────┐
│         Data Access Layer                   │
│  ┌──────────────┐  ┌──────────────┐        │
│  │ UART         │  │ EEPROM       │        │
│  │ Protocol     │  │ Manager      │        │
│  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────────┐
│         Hardware Abstraction Layer          │
│  ┌──────────────┐  ┌──────────────┐        │
│  │ WiFi Stack   │  │ Serial HAL   │        │
│  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────┘
```

### Core Components

#### 1. Main Loop (ASA0002E.ino)

```cpp
void setup() {
    // 1. Initialize Hardware
    Serial.begin(115200);
    pinMode(GWP, OUTPUT);
    
    // 2. Load Configuration
    Settings_Read();
    
    // 3. Initialize WiFi
    WiFi_Setup();
    
    // 4. Start Services
    server->begin();
    Telnet_Setup();
    initSSDP();
    
    // 5. Initialize UART Protocol
    InitializeUARTService();
}

void loop() {
    // 1. Process UART
    SerialPortReceiveProcess();
    
    // 2. Update Services (1 second interval)
    if (millis() - last1sMillis >= 1000) {
        // Network discovery
        MDNS.update();
        sendMNDP();
        
        // Weight updates
        Request_GiveWeight();
    }
    
    // 3. Update Services (100ms interval)
    if (millis() - last100msMillis >= 100) {
        // WebSocket updates
        ws.cleanupClients();
        
        // State updates
        updateSystemState();
    }
    
    // 4. Service Network
    Telnet_Service();
}
```

#### 2. UART Protocol Handler (UART.cpp)

State machine for parsing binary protocol:

```cpp
void SerialPortReceiveProcess() {
    while (Serial_Available()) {
        U8 receivedByte = Serial_Read();
        
        switch (eProcessState) {
            case WaitforSOH_:
                if (receivedByte == SOH) {
                    eProcessState = WaitforPrefix_;
                }
                break;
                
            case WaitforPrefix_:
                ePrefix = receivedByte;
                eProcessState = WaitforSuffix_;
                break;
                
            case WaitforSuffix_:
                eSuffix = receivedByte;
                determineNextState();
                break;
                
            case Waitfor4Num_:
                collectNumericData();
                break;
                
            case WaitforBulk_:
                collectBulkData();
                break;
                
            case WaitforEOT_:
                if (receivedByte == EOT) {
                    processMessage();
                    eProcessState = WaitforSOH_;
                }
                break;
        }
    }
}
```

#### 3. HTTP Request Handler (ASA0002E.ino)

```cpp
server->on("/endpoint", HTTP_GET, 
    [](AsyncWebServerRequest *request) {
        // 1. Validate parameters
        if (!request->hasArg("param")) {
            request->send(400, "text/plain", "missing param");
            return;
        }
        
        // 2. Process request
        String value = request->arg("param");
        bool result = processOperation(value);
        
        // 3. Send response
        request->send(200, "text/plain", 
                     result ? "ok!" : "fail");
    }
);
```

#### 4. WebSocket Handler (AsyncWebServer.cpp)

```cpp
void onWsEvent(AsyncWebSocket *server, 
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
    switch(type) {
        case WS_EVT_CONNECT:
            // Client connected
            client->text("status:" + String(eMasterStatus));
            break;
            
        case WS_EVT_DISCONNECT:
            // Client disconnected
            break;
            
        case WS_EVT_DATA:
            // Process command
            onWsReceivedCommand(client, (char*)data);
            break;
            
        case WS_EVT_ERROR:
            // Handle error
            break;
    }
}
```

### Module Dependencies

```
ASA0002E.ino
├── AsyncWebServer.h (HTTP server)
├── AsyncOTA.h (Firmware updates)
├── Authentication.h (Session management)
├── Sessions.h (User sessions)
├── Telnet.h (Telnet server)
├── Shell.h (Command shell)
├── UART.h (Serial protocol)
├── SSDP.h (Device discovery)
├── MNDP.h (MikroTik discovery)
└── CrashHandler.h (Error recovery)
```

## Communication Protocols

### UART Protocol Stack

```
┌─────────────────────────────────────┐
│   Application Messages              │
│   (Requests/Responses/Events)       │
└─────────────────────────────────────┘
                ↕
┌─────────────────────────────────────┐
│   Frame Layer                       │
│   SOH + Prefix + Suffix + EOT       │
└─────────────────────────────────────┘
                ↕
┌─────────────────────────────────────┐
│   Physical Layer                    │
│   UART 115200 8N1                   │
└─────────────────────────────────────┘
```

#### Frame Format

```
Byte 0:     SOH (0x01)
Byte 1:     Prefix (message type)
Byte 2:     Suffix (message subtype)
Bytes 3-N:  Payload (variable length)
Byte N+1:   EOT (0x04)
```

#### Message Types (Prefix)

| Prefix | Type | Direction | Description |
|--------|------|-----------|-------------|
| 0x01 | Request | ESP→PU | Command request |
| 0x02 | Response | PU→ESP | Command response |
| 0x03 | Event | PU→ESP | Unsolicited event |
| 0x04 | SetNum | ESP→PU | Set numeric value |
| 0x05 | SetStr | ESP→PU | Set string value |
| 0x06 | GetNum | ESP→PU | Request numeric value |
| 0x07 | GetStr | ESP→PU | Request string value |
| 0x08 | Bulk | PU→ESP | Bulk data transfer |
| 0x09 | File | PU→ESP | File data transfer |

### Network Protocols

#### HTTP/REST

```
Client → Server

GET /weight/get HTTP/1.1
Host: device-ip
Accept: text/plain


Server → Client

HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 5

12345
```

#### WebSocket

```
Client → Server (Handshake)

GET /ws HTTP/1.1
Host: device-ip
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13


Server → Client (Handshake)

HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=


Client → Server (Data)

[WebSocket Frame]
Opcode: Text (0x1)
Payload: "weight"


Server → Client (Data)

[WebSocket Frame]
Opcode: Text (0x1)
Payload: "12345"
```

#### Discovery Protocols

**SSDP (Simple Service Discovery Protocol)**:
```
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
MX: 3
ST: upnp:rootdevice
```

**mDNS (Multicast DNS)**:
```
Query: hostname.local (Type A)
Response: 192.168.1.100
```

**MNDP (MikroTik Neighbor Discovery)**:
```
TLV: MAC_ADDRESS (6 bytes)
TLV: IDENTITY (hostname)
TLV: VERSION (firmware version)
TLV: PLATFORM (ESP8266)
TLV: IPV4_ADDR (4 bytes)
```

## Data Flow

### Weight Reading Flow

```
┌──────────┐         ┌──────────┐         ┌──────────┐
│  Client  │         │  ESP8266 │         │  PU850   │
└─────┬────┘         └─────┬────┘         └────┬─────┘
      │                    │                    │
      │ GET /weight/get    │                    │
      ├───────────────────>│                    │
      │                    │ Request Weight     │
      │                    ├───────────────────>│
      │                    │                    │
      │                    │   Weight Value     │
      │                    │<───────────────────┤
      │    "12345"         │                    │
      │<───────────────────┤                    │
      │                    │                    │
```

### File Download Flow

```
┌──────────┐         ┌──────────┐         ┌──────────┐
│  Client  │         │  ESP8266 │         │  PU850   │
└─────┬────┘         └─────┬────┘         └────┬─────┘
      │                    │                    │
      │ GET /file/download │                    │
      ├───────────────────>│                    │
      │                    │ Request File       │
      │                    ├───────────────────>│
      │                    │                    │
      │                    │  File Header       │
      │                    │<───────────────────┤
      │  HTTP Headers      │                    │
      │<───────────────────┤                    │
      │                    │  File Data (chunk) │
      │                    │<───────────────────┤
      │  File Data         │                    │
      │<───────────────────┤                    │
      │                    │  File Data (chunk) │
      │                    │<───────────────────┤
      │  File Data         │                    │
      │<───────────────────┤                    │
      │                    │  End of File       │
      │                    │<───────────────────┤
      │  Connection Close  │                    │
      │                    │                    │
```

### WebSocket Real-Time Updates

```
┌──────────┐         ┌──────────┐         ┌──────────┐
│  Client  │         │  ESP8266 │         │  PU850   │
└─────┬────┘         └─────┬────┘         └────┬─────┘
      │                    │                    │
      │ WebSocket Connect  │                    │
      ├───────────────────>│                    │
      │    Connected       │                    │
      │<───────────────────┤                    │
      │                    │                    │
      │                    │  Weight Change     │
      │                    │<───────────────────┤
      │    "12345"         │                    │
      │<───────────────────┤                    │
      │                    │                    │
      │                    │  Weight Change     │
      │                    │<───────────────────┤
      │    "12350"         │                    │
      │<───────────────────┤                    │
      │                    │                    │
```

## Memory Management

### RAM Usage

```
Total RAM: 80 KB
├── System Reserved: ~32 KB
│   ├── WiFi stack: ~20 KB
│   ├── TCP/IP stack: ~8 KB
│   └── System overhead: ~4 KB
├── Heap: ~40 KB
│   ├── WebSocket buffers: ~8 KB
│   ├── HTTP buffers: ~8 KB
│   ├── UART buffers: ~2 KB
│   └── Dynamic allocations: ~22 KB
└── Stack: ~8 KB
    ├── Main task: ~4 KB
    └── System tasks: ~4 KB
```

### Flash Layout

```
Total Flash: 1 MB (1048576 bytes)
├── Bootloader: 0x00000 - 0x01000 (4 KB)
├── Firmware: 0x01000 - 0xC5000 (~784 KB)
├── EEPROM: 0xC5000 - 0xC6000 (4 KB)
└── Reserved: 0xC6000 - 0x100000 (~236 KB)
```

### EEPROM Layout

```
Settings Structure (stored at address 0x00):
├── CRC32: 4 bytes
├── net_flags: 1 byte
├── hostname: 33 bytes
├── ap_ssid: 33 bytes
├── ap_password: 33 bytes
├── sta_ssid: 33 bytes
└── sta_password: 33 bytes
Total: 170 bytes
```

### Buffer Management

```cpp
// UART circular buffer
#define UART_BUFFER_SIZE 1024
uint8_t uartBuffer[UART_BUFFER_SIZE];
uint16_t bufferHead = 0;
uint16_t bufferTail = 0;

// Bulk data buffer
#define BULK_BUFFER_SIZE 2048
uint8_t bulkBuffer[BULK_BUFFER_SIZE];
uint16_t bulkIndex = 0;

// WebSocket message buffer
#define WS_BUFFER_SIZE 512
char wsBuffer[WS_BUFFER_SIZE];
```

## Network Stack

### TCP/IP Stack

```
┌─────────────────────────────────────┐
│   Application Layer                 │
│   HTTP, WebSocket, Telnet, SSDP     │
└─────────────────────────────────────┘
                ↕
┌─────────────────────────────────────┐
│   Transport Layer                   │
│   TCP, UDP                          │
└─────────────────────────────────────┘
                ↕
┌─────────────────────────────────────┐
│   Internet Layer                    │
│   IP, ICMP, IGMP                    │
└─────────────────────────────────────┘
                ↕
┌─────────────────────────────────────┐
│   Link Layer                        │
│   WiFi MAC, 802.11 b/g/n           │
└─────────────────────────────────────┘
```

### Port Allocations

| Port | Service | Protocol | Purpose |
|------|---------|----------|---------|
| 23 | Telnet | TCP | Remote shell |
| 80 | HTTP | TCP | Web interface & API |
| 1900 | SSDP | UDP | Device discovery |
| 5353 | mDNS | UDP | Name resolution |
| 5678 | MNDP | UDP | MikroTik discovery |

### WiFi State Machine

```
┌─────────────┐
│ WIFI_OFF    │
└──────┬──────┘
       │ Enable WiFi
       ↓
┌─────────────┐
│ SCANNING    │
└──────┬──────┘
       │ SSID Found
       ↓
┌─────────────┐
│ CONNECTING  │
└──┬─────┬────┘
   │     │ Connected
   │     ↓
   │  ┌─────────────┐
   │  │ DHCP_REQ    │
   │  └──────┬──────┘
   │         │ IP Acquired
   │         ↓
   │  ┌─────────────┐
   │  │ CONNECTED   │
   │  └─────────────┘
   │ Failed
   ↓
┌─────────────┐
│ DISCONNECTED│
└─────────────┘
```

## Security Model

### Authentication Flow

```
┌─────────────┐
│ User Login  │
└──────┬──────┘
       │ POST /login
       ↓
┌─────────────────────────┐
│ Validate Credentials    │
└──────┬─────────┬────────┘
       │ Valid   │ Invalid
       ↓         ↓
┌─────────────┐ ┌─────────────┐
│ Create      │ │ Return      │
│ Session     │ │ Error       │
└──────┬──────┘ └─────────────┘
       │ Generate Token
       ↓
┌─────────────────────────┐
│ Store Session           │
│ - Token                 │
│ - Access Level          │
│ - IP Address            │
│ - Timestamp             │
└──────┬──────────────────┘
       │ Return Token
       ↓
┌─────────────┐
│ Client      │
│ Stores Token│
└─────────────┘
```

### Session Management

```cpp
// Session structure
typedef struct {
    char authToken[16];      // 128-bit random token
    byte accessLevel;        // 0-4 (access level)
    uint32_t lastActive;     // Unix timestamp
    IPAddress lastIP;        // Client IP
    bool isLoggedIn;         // Login status
} session;

// Session validation
int validateSession(const char* token, IPAddress ip) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (memcmp(sessions[i].authToken, token, 16) == 0) {
            if (sessions[i].lastIP == ip) {
                if (millis() - sessions[i].lastActive < SESSION_TIMEOUT) {
                    sessions[i].lastActive = millis();
                    return i;  // Valid session
                }
            }
        }
    }
    return -1;  // Invalid session
}
```

### Access Control

```
Access Levels:
├── Level 0 (Unknown): No access
├── Level 1 (Guest): Read-only
│   └── Can view: status, weight, info
├── Level 2 (User): Read + Basic operations
│   └── Can: tare, beep, view files
├── Level 3 (Admin): User + Configuration
│   └── Can: change settings, download files
└── Level 4 (Owner): Full control
    └── Can: reboot, reset, OTA update
```

## State Management

### Global State Variables

```cpp
// System state
U8 eMasterStatus = Busy_;        // PU850 status
bool initStatus = false;          // Initialization complete
bool firmwareIsValid = false;     // Firmware CRC OK

// Network state
U8 oldNetStatus = 0xff;          // Previous network status
bool isDHCPTimeout = false;      // DHCP timed out

// UART protocol state
U8 eProcessState = WaitforSOH_;  // Protocol state machine
U8 ePrefix, eSuffix;             // Current message type

// Weight state
S32 eWeightValue = UnDefinedNum_; // Current weight
bool eWeightReady = false;        // Weight value valid

// File transfer state
bool BulkDetected = false;        // Bulk transfer active
bool BulkOverflow = false;        // Buffer overflow
U32 BulkTotalRead = 0;           // Bytes received
U32 FinalDataSize = 0;           // Total expected bytes

// WebSocket state
U16 lastWsCount = 0;             // Connected clients
```

### State Transitions

```
Boot State Machine:
┌──────────┐
│ Power On │
└────┬─────┘
     │
     ↓
┌──────────────┐
│ Init Hardware│
└────┬─────────┘
     │
     ↓
┌──────────────┐
│ Load Settings│
└────┬─────────┘
     │
     ↓
┌──────────────┐
│ Setup WiFi   │
└────┬─────────┘
     │
     ↓
┌──────────────┐
│ Start Services│
└────┬─────────┘
     │
     ↓
┌──────────────┐
│ Init UART    │
└────┬─────────┘
     │
     ↓
┌──────────────┐
│ Ready        │
└──────────────┘
```

## Performance Considerations

### Response Times

| Operation | Typical | Maximum | Notes |
|-----------|---------|---------|-------|
| HTTP request | 10ms | 100ms | Simple endpoints |
| Weight reading | 50ms | 200ms | Depends on PU850 |
| File download | - | - | Network limited |
| WebSocket message | 5ms | 50ms | Near real-time |
| UART response | 20ms | 1000ms | Protocol timeout |
| OTA update | 30s | 60s | 1MB firmware |

### Throughput

- **HTTP**: ~100 KB/s (network limited)
- **WebSocket**: ~50 KB/s sustained
- **UART**: ~11.5 KB/s (115200 baud)
- **File download**: Limited by UART speed

### Optimization Strategies

1. **Async Operations**: All network I/O is asynchronous
2. **Event-Driven**: No polling loops, only event handlers
3. **Buffer Management**: Fixed-size buffers, no dynamic allocation in loops
4. **String Optimization**: Use `PROGMEM` for constant strings
5. **Memory Pool**: Pre-allocate buffers at startup
6. **Lazy Evaluation**: Only compute values when needed
7. **Caching**: Cache frequently accessed values

### Bottlenecks

1. **UART Speed**: Limited to 115200 baud
2. **RAM**: Only 40 KB usable heap
3. **WiFi**: 2.4 GHz congestion
4. **CPU**: Single-threaded execution
5. **Flash Writes**: Limited to EEPROM emulation

### Scaling Limits

- **Max WebSocket Clients**: 10 (configured)
- **Max HTTP Connections**: 5 (TCP stack limit)
- **Max Sessions**: 8 (configured)
- **File Transfer Size**: Limited by available heap
- **Bulk Data Buffer**: 2KB maximum

---

## Design Decisions

### Why Async Web Server?
- Non-blocking I/O
- Better memory efficiency
- Higher concurrency
- Lower latency

### Why State Machine for UART?
- Predictable behavior
- Easy to debug
- Handles partial frames
- Memory efficient

### Why Token-Based Auth?
- Stateless clients
- Easy to implement
- Secure random tokens
- Session timeout support

### Why Multiple Discovery Protocols?
- Maximum compatibility
- Different network types
- User convenience
- Enterprise support

---

## Future Enhancements

### Planned Features
1. TLS/HTTPS support
2. User management database
3. Logging and analytics
4. Cloud connectivity
5. Firmware auto-update
6. Configuration backup/restore

### Performance Improvements
1. Response caching
2. Compression (gzip)
3. Connection pooling
4. Batch operations
5. Optimized protocol

### Security Enhancements
1. Certificate pinning
2. Rate limiting
3. Brute force protection
4. Encrypted storage
5. Secure boot

---

For more information:
- [Developer Guide](DEVELOPER_GUIDE.md)
- [API Reference](API_REFERENCE.md)
- [User Guide](USER_GUIDE.md)

Contact: David@Refoua.me
