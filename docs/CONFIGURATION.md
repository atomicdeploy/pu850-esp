# Configuration Guide

## Table of Contents

1. [Initial Setup](#initial-setup)
2. [Network Configuration](#network-configuration)
3. [UART Configuration](#uart-configuration)
4. [Build Configuration](#build-configuration)
5. [Advanced Settings](#advanced-settings)
6. [Configuration Files](#configuration-files)

## Initial Setup

### First Boot Configuration

On the first boot, the ESP8266 will:
1. Initialize with default settings
2. Create an Access Point with default credentials
3. Display connection information on PU850 screen

**Default Settings**:
- **WiFi Mode**: Access Point (AP)
- **AP SSID**: Generated from hostname
- **AP Password**: Shown on PU850 display
- **AP IP**: 192.168.4.1
- **Hostname**: Based on device serial number

### Configuration Workflow

```
Power On Device
      ↓
Connect to AP
      ↓
Open Web Browser → 192.168.4.1
      ↓
Configure WiFi Settings
      ↓
Save Configuration
      ↓
Device Reboots
      ↓
Connect to Your Network
```

## Network Configuration

### WiFi Settings

#### Station Mode (Client)
Connect to existing WiFi network:

```cpp
// Configuration stored in EEPROM
flashSettings.sta_ssid = "YourWiFiSSID";
flashSettings.sta_password = "YourPassword";
flashSettings.net_flags |= 0x01;  // Enable Station mode
```

**Required Parameters**:
- SSID (max 32 characters)
- Password (max 32 characters)
- DHCP enabled/disabled

**Optional Parameters**:
- Static IP address
- Subnet mask
- Gateway
- DNS servers

#### Access Point Mode
Create your own WiFi network:

```cpp
// Configuration stored in EEPROM
flashSettings.ap_ssid = "PU850-Device";
flashSettings.ap_password = "SecurePassword";
flashSettings.net_flags |= 0x02;  // Enable AP mode
```

**Required Parameters**:
- SSID (max 32 characters)
- Password (min 8 characters, max 32)

**Optional Parameters**:
- Channel (1-13, default: auto)
- Hidden SSID (default: visible)
- Max connections (default: 4)

#### Hybrid Mode (STA+AP)
Enable both modes simultaneously:

```cpp
flashSettings.net_flags = 0x03;  // Enable both STA and AP
```

**Use Cases**:
- Initial configuration while maintaining connectivity
- Fallback access if station connection fails
- Development and debugging

### DHCP Configuration

#### Enable DHCP (Dynamic IP)
```cpp
flashSettings.net_flags |= 0x04;  // Enable DHCP
```

**Advantages**:
- Automatic IP assignment
- No manual configuration
- Adapts to network changes

**Disadvantages**:
- IP may change after reboot
- Depends on DHCP server
- Slight connection delay

#### Static IP Configuration
```cpp
flashSettings.net_flags &= ~0x04;  // Disable DHCP

// Set static IP (future implementation)
// IPAddress staticIP(192, 168, 1, 100);
// IPAddress gateway(192, 168, 1, 1);
// IPAddress subnet(255, 255, 255, 0);
// IPAddress dns1(8, 8, 8, 8);
// IPAddress dns2(8, 8, 4, 4);
```

**Advantages**:
- Consistent IP address
- Faster connection
- No DHCP dependency

**Disadvantages**:
- Manual configuration required
- Must avoid IP conflicts
- Network-specific settings

### Network Flags

The `net_flags` byte controls network behavior:

```
Bit 0: Station Mode (0=disabled, 1=enabled)
Bit 1: Access Point Mode (0=disabled, 1=enabled)
Bit 2: DHCP (0=disabled, 1=enabled)
Bit 3: Reserved
Bit 4: Reserved
Bit 5: Reserved
Bit 6: Reserved
Bit 7: Reserved
```

**Examples**:
```cpp
0x01 = Station only, no DHCP
0x03 = Station + AP, no DHCP
0x05 = Station only, with DHCP
0x07 = Station + AP, with DHCP
0x02 = AP only
```

### Hostname Configuration

#### Setting Hostname
```cpp
strncpy(flashSettings.hostname, "pu850-device", 
        sizeof(flashSettings.hostname) - 1);
```

**Requirements** (RFC952):
- Start with letter (a-z, A-Z)
- Contains only letters, numbers, hyphens
- No trailing hyphen
- Maximum 24 characters
- No spaces

**Examples**:
```
✓ Valid:   pu850-scale, Device123, MyScale-01
✗ Invalid: 850pu, -device, device-, my device
```

### Discovery Services

#### mDNS (Multicast DNS)
Enables `.local` domain access:
```
http://[hostname].local
```

**Configuration**:
```cpp
MDNS.begin(WiFi.hostname().c_str());
MDNS.addService("http", "tcp", 80);
```

#### NetBIOS
Windows network discovery:
```cpp
NBNS.begin(WiFi.hostname().c_str());
```

#### LLMNR
Link-Local Multicast Name Resolution:
```cpp
LLMNR.begin(WiFi.hostname().c_str());
```

#### SSDP
UPnP device discovery:
```cpp
SSDP.setName(flashSettings.hostname);
SSDP.setSerialNumber(E_SerialNumber);
SSDP.setModelName("PU850-ESP8266");
SSDP.setManufacturer("Faravary Pand Caspian");
```

#### MNDP
MikroTik Neighbor Discovery:
```cpp
setupMNDP();  // Auto-configured with device info
```

## UART Configuration

### Serial Port Settings

**Default Configuration**:
```cpp
BAUD_RATE = 115200;
Serial.begin(BAUD_RATE);
Serial.setRxBufferSize(UART_BUFFER_SIZE);  // 1024 bytes
```

**Parameters**:
- **Baud Rate**: 115200 bps (configurable)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Flow Control**: None

### Buffer Configuration

```cpp
#define UART_BUFFER_SIZE 1024        // RX buffer
#define UART_THRESHOLD_BUSY 512       // Busy threshold
#define BULK_BUFFER_SIZE 2048         // Bulk data
```

**Buffer Sizes**:
- **UART RX**: 1024 bytes (circular buffer)
- **Bulk Data**: 2048 bytes (file transfers)
- **Command**: 128 bytes (commands/responses)

### Protocol Timeouts

```cpp
#define validTimeOut_ 1000L  // 1 second
```

**Timeout Values**:
- **Command Response**: 1000ms
- **Bulk Data**: 2000ms (double timeout)
- **File Transfer**: 2000ms
- **Idle Detection**: 100ms

### Hardware Pins

```cpp
const int MRST = 5;   // GPIO 5 - PU850 Reset
const int GWP = 16;   // GPIO 16 - Status Pin
```

**Pin Functions**:
- **MRST (GPIO 5)**: Hardware reset output to PU850
  - HIGH: Normal operation
  - LOW: Reset PU850
  
- **GWP (GPIO 16)**: Status signal to PU850
  - LOW: ESP ready
  - HIGH: ESP busy/not ready

## Build Configuration

### Compiler Flags

#### Standard Build
```bash
./build.sh
```

**Default Flags**:
```
-DARDUINO_CLI
-DUSE_LOCALH
-Wall
```

#### Debug Build
```bash
./build.sh --debug-tools
```

**Additional Flags**:
```
-DDebugTools       # Enable debug endpoints
```

**Debug Endpoints**:
- `/debug/info` - Buffer information
- `/debug/data` - Debug log contents
- `/debug/clear` - Clear debug buffer
- `/eProcessState` - UART state
- `/eResponseStatus` - Response status
- `/crash` - Test crash handler

#### Serial Debug
```bash
./build.sh --debug-on-serial
```

**Additional Flags**:
```
-DDebugOnSerial    # Output debug to serial
```

**Note**: Disables UART communication with PU850

#### Shell on Serial
```bash
./build.sh --shell-on-serial
```

**Additional Flags**:
```
-DShellOnSerial    # Enable shell on serial
```

**Note**: For development without PU850 hardware

### Board Configuration

```bash
# FQBN (Fully Qualified Board Name)
esp8266:esp8266:generic

# Board Parameters
xtal=80                 # 80 MHz crystal
FlashFreq=40            # 40 MHz flash
FlashMode=qio           # QIO flash mode
eesz=1M                 # 1 MB flash size
led=2                   # GPIO 2 for LED
baud=115200             # Upload baud rate
```

### Memory Configuration

```cpp
// Flash Size Options
// 512K (64K SPIFFS)
// 1M (64K SPIFFS)    ← Selected
// 2M (1M SPIFFS)
// 4M (1M SPIFFS)
// 4M (3M SPIFFS)
```

### Library Versions

Specified in `sketch.yaml`:
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

## Advanced Settings

### Session Management

```cpp
const int MAX_SESSIONS = 8;
const int TOKEN_LENGTH = 32;  // 16 bytes, 32 hex chars
```

**Session Configuration**:
- Max concurrent sessions: 8
- Token size: 128 bits (16 bytes)
- Token format: Hexadecimal string
- Session timeout: Configurable (default: 3600s)

### WebSocket Configuration

```cpp
const int MAX_WS_CLIENTS = 10;
AsyncWebSocket ws("/ws");
```

**WebSocket Settings**:
- Max clients: 10
- Endpoint: `/ws`
- Auto-ping: Enabled
- Message buffer: 512 bytes

### HTTP Server Configuration

```cpp
U16 HTTP_Port = 80;
AsyncWebServer *server = new AsyncWebServer(HTTP_Port);
```

**Server Settings**:
- Port: 80 (configurable)
- Max connections: 5 (TCP stack limit)
- Request timeout: 5 seconds
- Body size limit: 8KB

### Telnet Server Configuration

```cpp
U16 Telnet_Port = 23;
ESPTelnet telnet;
```

**Telnet Settings**:
- Port: 23 (configurable)
- Max connections: 1
- Echo: Enabled
- Line mode: Enabled

### CORS Configuration

```cpp
cors.setOrigin("*");
cors.setMethods("GET, POST, OPTIONS");
cors.setHeaders("*");
cors.setMaxAge(600);
```

**CORS Settings**:
- Allow all origins
- Methods: GET, POST, OPTIONS
- Headers: All
- Max age: 600 seconds

### Firmware Update Settings

```cpp
// OTA Update Configuration
AsyncOTA.begin(server);

// Update endpoint: /update
// Max upload size: Free sketch space
// MD5 verification: Enabled
// Auto-reboot: Enabled
```

## Configuration Files

### EEPROM Storage

**Structure**:
```cpp
struct flashSettings {
    U32 crc32;                    // CRC32 checksum
    U8 net_flags;                 // Network flags
    C8 hostname[FCSTS_ + 1];      // Device hostname
    C8 ap_ssid[FCSTS_ + 1];       // AP SSID
    C8 ap_password[FCSTS_ + 1];   // AP password
    C8 sta_ssid[FCSTS_ + 1];      // Station SSID
    C8 sta_password[FCSTS_ + 1];  // Station password
};
```

**Size**: 170 bytes

### Reading Configuration

```cpp
bool Settings_Read() {
    EEPROM.get(0, flashSettings);
    
    // Verify CRC32
    U32 crc = calculateCRC32(
        ((U8*)&flashSettings) + sizeof(flashSettings.crc32),
        sizeof(flashSettings) - sizeof(flashSettings.crc32)
    );
    
    if (crc == flashSettings.crc32) {
        return true;  // Valid configuration
    }
    
    // Invalid or corrupted, use defaults
    Settings_Clear();
    return false;
}
```

### Writing Configuration

```cpp
bool Settings_Write() {
    // Calculate CRC32
    flashSettings.crc32 = calculateCRC32(
        ((U8*)&flashSettings) + sizeof(flashSettings.crc32),
        sizeof(flashSettings) - sizeof(flashSettings.crc32)
    );
    
    // Write to EEPROM
    EEPROM.put(0, flashSettings);
    
    // Commit changes
    return EEPROM.commit();
}
```

### Configuration Workflow

```
Read Configuration
      ↓
Valid CRC? ──No──→ Load Defaults
      │                  ↓
     Yes            Save Defaults
      │                  ↓
      └──────────────────┤
                         ↓
              Apply Configuration
                         ↓
              Start Network Services
```

### Backup and Restore

#### Backup via HTTP
```bash
# Get current configuration (manual process)
curl http://device-ip/config/st > station.txt
curl http://device-ip/config/ap > ap.txt
curl http://device-ip/info > device-info.txt
```

#### Restore Process
1. Power on device
2. Connect to AP mode
3. Navigate to configuration page
4. Enter saved settings
5. Save and reboot

### Factory Reset

#### Method 1: Via Code
```cpp
// Clear settings
Settings_Clear();
Settings_Write();

// Restart
ESP.restart();
```

#### Method 2: Via Hardware
1. Hold reset button
2. Power cycle device
3. Release after 10 seconds

#### Method 3: Via Reflash
```bash
# Erase flash completely
esptool.py --port /dev/ttyUSB0 erase_flash

# Flash new firmware
esptool.py --port /dev/ttyUSB0 write_flash 0x00000 firmware.bin
```

## Configuration Best Practices

### Security
1. **Change default passwords** immediately
2. **Use strong WiFi passwords** (12+ characters, mixed case, numbers, symbols)
3. **Disable AP mode** when not needed
4. **Use static IP** for production deployments
5. **Limit WebSocket connections** to trusted clients

### Reliability
1. **Document your configuration** before changes
2. **Test configuration** in AP mode first
3. **Monitor signal strength** (-60 dBm or better)
4. **Use DHCP** unless static IP required
5. **Keep firmware updated**

### Performance
1. **Disable unused services**
2. **Reduce WebSocket clients** if experiencing issues
3. **Use wired connection** when possible
4. **Minimize debug output** in production
5. **Monitor memory usage**

### Network
1. **Use dedicated 2.4GHz channel** to avoid interference
2. **Place device** within good WiFi range
3. **Avoid channel congestion** (use 1, 6, or 11)
4. **Configure QoS** on router for device traffic
5. **Use static DHCP reservation** instead of static IP

---

## Troubleshooting Configuration Issues

### Configuration Won't Save
1. Check EEPROM size allocation
2. Verify CRC32 calculation
3. Check for flash write errors
4. Try factory reset
5. Reflash firmware

### Network Won't Connect
1. Verify SSID and password
2. Check WiFi signal strength
3. Verify 2.4GHz network (not 5GHz)
4. Check DHCP server
5. Try static IP configuration

### Settings Lost After Reboot
1. CRC verification failing
2. Flash corruption
3. Power issues during write
4. Try Settings_Write() multiple times
5. Reflash firmware

---

For more information:
- [User Guide](USER_GUIDE.md)
- [Developer Guide](DEVELOPER_GUIDE.md)
- [Architecture](ARCHITECTURE.md)

Contact: David@Refoua.me
