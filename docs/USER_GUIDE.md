# PU850 ESP8266 User Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Network Configuration](#network-configuration)
4. [Web Interface](#web-interface)
5. [Features and Functions](#features-and-functions)
6. [Troubleshooting](#troubleshooting)
7. [Maintenance](#maintenance)

## Introduction

The PU850 ESP8266 firmware enables WiFi connectivity for your PU850 electronic scale, allowing you to:

- Monitor weight readings in real-time from any device
- Access scale functions remotely via web interface
- Download receipts and reports
- Update firmware over-the-air
- Configure network settings
- View system information and diagnostics

## Getting Started

### Initial Power-On

When the device is first powered on:

1. The ESP8266 will attempt to connect to a previously configured WiFi network
2. If no configuration exists, it will create an Access Point (AP)
3. The PU850 display will show the WiFi status and connection details

### Connecting to the Device

#### Method 1: Access Point Mode

If the device is in AP mode:

1. On your phone/tablet/computer, look for a WiFi network starting with the hostname (check PU850 display)
2. Connect using the password shown on the display
3. Open a web browser and navigate to `http://192.168.4.1`

#### Method 2: Station Mode

If connected to your WiFi network:

1. Find the device IP address from:
   - PU850 display
   - Your router's DHCP client list
   - mDNS: `http://[hostname].local`
2. Open a web browser and navigate to the IP address

### Initial Configuration

On first access:

1. Navigate to the network settings page
2. Configure your WiFi credentials:
   - **SSID**: Your WiFi network name
   - **Password**: Your WiFi password
   - **Hostname**: Device name (optional)
3. Choose network mode:
   - **Station (STA)**: Connect to existing WiFi
   - **Access Point (AP)**: Create own WiFi network
   - **Both (STA+AP)**: Hybrid mode
4. Enable/disable DHCP for automatic IP addressing
5. Click **Save** and reboot

## Network Configuration

### WiFi Modes

#### Station Mode (STA)
- Connects to your existing WiFi network
- Gets IP address from your router (DHCP) or uses static IP
- Access via network IP address or hostname.local

#### Access Point Mode (AP)
- Creates its own WiFi network
- Default IP: 192.168.4.1
- Allows direct connection without existing network

#### Hybrid Mode (STA+AP)
- Maintains both connections simultaneously
- Best for setup and troubleshooting
- Slightly higher power consumption

### Network Settings

#### Hostname
- Default: Based on device serial number
- Used for mDNS (.local) access
- Must follow RFC952 naming rules:
  - Start with letter
  - Letters, numbers, hyphens only
  - Max 24 characters
  - No spaces

#### DHCP (Dynamic IP)
- Automatic IP address assignment
- Recommended for most users
- May change after router restart

#### Static IP
- Fixed IP address
- Requires manual configuration:
  - IP Address
  - Subnet Mask
  - Gateway
  - DNS Servers

### Network Discovery

The device announces itself using multiple protocols:

- **mDNS/Bonjour**: `http://[hostname].local`
- **NetBIOS**: Windows network discovery
- **LLMNR**: Link-Local Multicast Name Resolution
- **SSDP/UPnP**: Universal Plug and Play
- **MNDP**: MikroTik Neighbor Discovery

## Web Interface

### Dashboard

The main dashboard shows:

- Current weight reading
- Device status
- Network information
- Battery/power status
- Quick action buttons

### Features

#### Real-Time Weight Display
- Live weight updates via WebSocket
- Auto-updates every second
- Shows weight in configured units
- Indicates stable readings

#### System Information
- Firmware version and hash
- ESP8266 chip ID
- Network status and signal strength
- Memory usage
- Uptime

#### Quick Actions
- **Tare**: Zero the scale
- **Beep**: Test buzzer
- **Show/Hide Weight**: Toggle display
- **Send Message**: Display text on PU850

## Features and Functions

### Weight Operations

#### Get Current Weight
```
Endpoint: GET /weight/get
Returns: Current weight value or "fail"
```

#### Tare (Zero) Scale
```
Endpoint: POST /action/tare
Returns: "ok!" or "fail"
```

#### Show/Hide Weight Display
```
Endpoint: GET /weight/display
Returns: "shown", "hidden", or "unknown"

Endpoint: POST /weight/display?value=show
Endpoint: POST /weight/display?value=hide
Returns: "ok!" or "fail"
```

### File Operations

#### Download Receipt
```
Endpoint: GET /receipt?id=[receipt_id]
Returns: Receipt file as binary data
```

Receipts can be:
- Printed receipts
- Transaction records
- Archived data

#### Download Files
```
Endpoint: GET /file/download?filename=[filename]
Returns: File as binary data with proper headers
Supports: Range requests for partial downloads
```

#### Cancel File Transfer
```
Endpoint: GET /file/cancel
Returns: "canceled"
```

Use this to abort an ongoing file download operation.

### Date and Time

#### Get Date/Time
```
Endpoint: GET /datetime/get
Returns: "yyyy/mm/dd hh:mm:ss" or "fail"
```

#### Set Date/Time
```
Endpoint: POST /datetime/set?val=yyyy/mm/dd%20hh:mm:ss
Returns: "ok!" or "fail"
```

Format: `YYYY/MM/DD HH:MM:SS` (URL encoded)

### System Control

#### Reboot PU850 Main Unit
```
Endpoint: POST /pu_reboot
Returns: "ok!", "fail", "forbidden", or "not acceptable"
```

**Note**: Requires appropriate access level.

#### Reset PU850 (Hardware Reset)
```
Endpoint: POST /pu_reset
Returns: "ok!"
```

Performs a hardware reset of the PU850 main unit.

#### Send Message to Display
```
Endpoint: POST /message/send?text=[message]
Returns: "ok!" or "fail"
```

Displays a text message on the PU850 screen.

#### Beep
```
Endpoint: GET /beep?count=[number]
Returns: "beep"
```

Makes the device beep N times (default: 1).

### Configuration

#### Get System Language
```
Endpoint: GET /lang
Returns: Language code (E/F/A/R) or "fail"
```

#### Set System Language
```
Endpoint: POST /lang?lang=[E|F|A|R]
Returns: "ok!" or "fail"
```

Languages:
- E: English
- F: French
- A: Arabic
- R: Russian (Farsi)

#### Get Current Page ID
```
Endpoint: GET /page_id
Returns: Page index or "fail"
```

#### Navigate to Page
```
Endpoint: POST /page_goto?pu_id=[page_id]
Returns: "ok!" or "fail"
```

### Firmware Updates

#### Check Update Information
```
Endpoint: GET /update/info
Returns: JSON with firmware hash, size, free space, build date
```

#### Perform OTA Update
Navigate to: `http://[device-ip]/update`

1. Click "Choose File"
2. Select the `.bin` firmware file
3. Click "Update"
4. Wait for upload and verification
5. Device will reboot automatically

**Important:**
- Do not power off during update
- Use firmware built for this hardware
- Verify MD5 checksum if possible
- Keep backup of current firmware

### Network Information

#### WiFi Signal Strength
```
Endpoint: GET /wifi/signal
Returns: RSSI value and quality description
```

Quality levels:
- **Excellent**: -40 dBm or better
- **Good**: -40 to -50 dBm
- **Normal**: -50 to -60 dBm
- **Low**: -60 to -70 dBm
- **Bad**: -70 to -80 dBm
- **No signal**: -110 dBm or worse

#### WiFi Status
```
Endpoint: GET /wifi/status
Returns: AP and Station status
```

#### Network Configuration
```
Endpoint: GET /config/ap  # Access Point config
Endpoint: GET /config/st  # Station config
Returns: Network settings including IP, mask, gateway
```

#### Device Information
```
Endpoint: GET /info
Returns: Comprehensive device information
```

Includes:
- Hostname and MAC addresses
- Firmware version and hash
- Protocol revision
- Flash size and usage
- Network settings
- Uptime
- Build information

## Troubleshooting

### Cannot Connect to WiFi

**Symptoms**: Device doesn't connect to your WiFi network

**Solutions**:
1. Verify SSID and password are correct
2. Check WiFi signal strength (must be adequate)
3. Ensure 2.4GHz WiFi (ESP8266 doesn't support 5GHz)
4. Check router compatibility (some enterprise networks may not work)
5. Try disabling MAC filtering on router temporarily
6. Check DHCP server has available addresses

### Cannot Access Web Interface

**Symptoms**: Browser cannot reach device IP

**Solutions**:
1. Verify device IP address from PU850 display
2. Ensure computer is on same network
3. Try mDNS: `http://[hostname].local`
4. Check firewall settings on computer
5. Try different browser
6. Clear browser cache
7. Disable browser extensions

### Weight Not Updating

**Symptoms**: Weight display shows "off" or doesn't update

**Solutions**:
1. Check UART connection to PU850
2. Verify PU850 is powered and functioning
3. Check serial communication settings
4. Restart both ESP8266 and PU850
5. Enable debug mode to see UART traffic

### OTA Update Fails

**Symptoms**: Update doesn't complete or device doesn't reboot

**Solutions**:
1. Ensure stable network connection
2. Use smaller firmware files if possible
3. Try with debug mode disabled
4. Verify firmware file is not corrupted
5. Check available heap memory
6. Power cycle device and retry

### High Memory Usage

**Symptoms**: Device crashes or becomes unstable

**Solutions**:
1. Reduce number of simultaneous WebSocket connections
2. Disable debug features if enabled
3. Limit file transfer size
4. Restart device periodically
5. Check for memory leaks in custom modifications

### WebSocket Disconnections

**Symptoms**: Real-time updates stop working

**Solutions**:
1. Check network stability
2. Reduce client-side connection timeout
3. Verify client WebSocket implementation
4. Monitor ESP8266 CPU usage
5. Ensure adequate power supply

## Maintenance

### Regular Tasks

#### Weekly
- Check device connectivity and accessibility
- Verify weight readings are accurate
- Test critical functions

#### Monthly
- Review system logs (if enabled)
- Check firmware for updates
- Backup configuration settings
- Clean device (dust, debris)

#### Quarterly
- Verify all network services working
- Test OTA update process (in safe environment)
- Review access logs for unauthorized access
- Update documentation of network changes

### Backup and Recovery

#### Backup Configuration
1. Document current network settings
2. Save WiFi credentials securely
3. Export any custom configurations
4. Take screenshots of web interface settings

#### Recovery Procedure
If device becomes inaccessible:
1. Power cycle the device
2. If still inaccessible, perform factory reset:
   - Use hardware reset pin
   - Or reflash firmware via USB
3. Reconfigure network settings
4. Restore backed-up configuration

### Best Practices

1. **Keep firmware updated**: Install updates when available
2. **Document changes**: Keep notes on configuration changes
3. **Test before deploying**: Verify updates in test environment
4. **Monitor regularly**: Check device health periodically
5. **Secure access**: Use strong passwords and session tokens
6. **Backup regularly**: Save configuration before making changes

### Support

For technical support:
- Check online documentation
- Review developer forums
- Contact manufacturer: Faravary Pand Caspian
- Email: David@Refoua.me

### Safety Warnings

⚠️ **Important Safety Information**

- Do not open device case while powered
- Use only specified power supply
- Keep away from water and moisture
- Do not modify hardware without expertise
- Disconnect power before servicing
- Follow local electrical safety regulations
