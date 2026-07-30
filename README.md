# PU850 ESP8266 Firmware

**Version:** 2.1.0  
**Chip:** ESP8266  
**Project Code:** ASA0002E

## Overview

This firmware provides WiFi connectivity and remote management capabilities for the PU850 electronic scale/weighing system. The ESP8266 acts as a bridge between the main PU850 unit and network clients, enabling remote monitoring, control, and data retrieval through a comprehensive web-based interface and RESTful API.

## Key Features

- **WiFi Connectivity**: Supports both Station (STA) and Access Point (AP) modes
- **Web Interface**: Responsive web interface for device management
- **RESTful API**: Comprehensive HTTP API for remote control and monitoring
- **WebSocket Support**: Real-time weight updates and device status
- **OTA Updates**: Over-the-Air firmware updates
- **Telnet Shell**: Remote command-line interface for debugging and management
- **UART Protocol**: Custom protocol for communication with PU850 main unit
- **Network Discovery**: SSDP, MDNS, NetBIOS, LLMNR, and MNDP support
- **Session Management**: Token-based authentication for secure access
- **File Operations**: Download receipts, reports, and files from PU850
- **Configuration Management**: Persistent settings stored in EEPROM

## Quick Start

### Prerequisites

- Arduino CLI v1.0.0 or later
- ESP8266 board support installed
- Required libraries (see `sketch.yaml`)

### Building

```bash
# Linux/macOS
./build.sh

# Windows
build.cmd
```

Build options:
- `--debug-tools`: Enable debug endpoints
- `--debug-on-serial`: Output debug messages to serial
- `--shell-on-serial`: Enable shell on serial instead of UART protocol

### Flashing

Upload the generated binary (`Build/ASA0002E.ino.bin`) to your ESP8266 using:
- Arduino IDE
- esptool.py
- OTA update (for subsequent updates)

### First-Time Setup

1. On first boot, the device will create a WiFi access point
2. Connect to the AP (check PU850 display for SSID/password)
3. Navigate to `http://192.168.4.1` in your browser
4. Configure your WiFi credentials and other settings
5. Save and reboot

## Documentation

- **[User Guide](docs/USER_GUIDE.md)**: End-user documentation for operating the device
- **[Developer Guide](docs/DEVELOPER_GUIDE.md)**: Technical documentation for developers
- **[API Reference](docs/API_REFERENCE.md)**: Complete HTTP API documentation
- **[Architecture](docs/ARCHITECTURE.md)**: System architecture and design overview

## Hardware

- **Module**: Generic ESP8266 Module
- **Flash Size**: 1MB
- **CPU Clock**: 80 MHz
- **Flash Mode**: QIO
- **Upload Speed**: 115200 baud
- **UART Baud Rate**: 115200 baud

### Pin Connections

- **GPIO 5 (MRST)**: Hardware reset pin for PU850 main unit
- **GPIO 16 (GWP)**: Hardware status pin to PU850
- **UART RX/TX**: Serial communication with PU850 main unit

## Project Structure

```
pu850-esp/
├── ASA0002E.ino          # Main firmware file
├── ASA0002E.h            # Main header file
├── defines.h             # Common definitions
├── LocalLib/             # Local library implementations
│   ├── AsyncOTA.*        # OTA update handler
│   ├── AsyncWebServer.*  # Web server configuration
│   ├── Authentication.*  # Session and auth management
│   ├── Commands.*        # Command processing
│   ├── CrashHandler.*    # Crash detection and reporting
│   ├── MNDP.*           # MikroTik Neighbor Discovery Protocol
│   ├── Sessions.*        # Session management
│   ├── Shell.*          # Telnet shell implementation
│   ├── SSDP.*           # SSDP device discovery
│   ├── Telnet.*         # Telnet server
│   └── UART.*           # UART protocol implementation
├── Temp/                 # Shared protocol definitions
├── build.sh             # Linux/macOS build script
├── build.cmd            # Windows build script
└── docs/                # Documentation files
```

## Communication Protocol

The ESP8266 communicates with the PU850 main unit using a custom UART protocol:

- **Baud Rate**: 115200
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Protocol**: Binary protocol with SOH/EOT framing
- **Buffer Size**: 1024 bytes

See [UART.cpp](LocalLib/UART.cpp) for protocol implementation details.

## Network Services

### HTTP Server (Port 80)
- RESTful API endpoints
- WebSocket endpoint at `/ws`
- Static file serving
- CORS support

### Telnet Server (Port 23)
- Command-line interface
- System diagnostics
- Configuration management

### Discovery Services
- **SSDP**: UPnP device discovery
- **mDNS**: `.local` domain resolution
- **NetBIOS**: Windows network discovery
- **LLMNR**: Link-Local Multicast Name Resolution
- **MNDP**: MikroTik Neighbor Discovery Protocol

## Security

- Token-based session authentication
- Configurable access levels
- Session timeout management
- Secure OTA updates with MD5 verification

## Development

### Code Style
- Follow existing code patterns
- Use descriptive variable names
- Comment complex logic
- Maintain compatibility with ESP8266 limitations

### Testing
- Test on actual hardware before deployment
- Verify UART communication with PU850
- Check memory usage and heap fragmentation
- Validate network operations

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Troubleshooting

### Common Issues

**Device not connecting to WiFi**
- Check SSID and password configuration
- Verify WiFi signal strength
- Check DHCP timeout settings

**UART communication errors**
- Verify baud rate (115200)
- Check physical connections
- Monitor debug output if enabled

**OTA update fails**
- Ensure sufficient free heap
- Verify MD5 checksum
- Check network connectivity

**WebSocket disconnections**
- Check network stability
- Verify client implementation
- Monitor ESP8266 heap usage

## License

Copyright © 2024 Faravary Pand Caspian  
All rights reserved.

## Contact

**Author**: David@Refoua.me  
**Ordered from**: Faravary Pand Caspian

## Acknowledgments

- ESP8266 Community
- Arduino Core for ESP8266
- ESPAsyncWebServer library developers
- All open-source contributors
