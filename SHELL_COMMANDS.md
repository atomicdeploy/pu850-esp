# Shell Commands Documentation

This document describes the shell commands available in the PU850-ESP firmware.

## Command List

### Information Commands

#### `help`
Shows a list of all available shell commands with brief descriptions.

**Usage:** `help`

#### `info`
Displays comprehensive system information including:
- Chip ID
- Flash ID and size
- Sketch size and free space
- Free heap memory
- Firmware hash
- Build date and time
- System uptime
- Last reset reason

**Usage:** `info`

#### `wifi`
Displays WiFi connection information including:
- Hostname
- Station MAC address and IP
- Connected SSID and signal strength (RSSI)
- Access Point MAC and IP
- Number of connected AP clients

**Usage:** `wifi`

#### `mem`
Displays detailed memory information including:
- Free heap memory
- Heap fragmentation percentage
- Maximum free block size
- Flash and sketch size details
- Program usage percentage

**Usage:** `mem`

#### `version`
Shows version information including:
- Firmware hash
- Build date and time
- SDK version
- Core version
- Boot version and mode

**Usage:** `version`

#### `reset`
Displays information about the last system reset:
- Reset reason
- Detailed reset information

**Usage:** `reset`

#### `uptime`
Shows the system uptime in a human-readable format (days, hours, minutes, seconds).

**Usage:** `uptime`

#### `free`
Quick display of available heap memory.

**Usage:** `free`

#### `list`
Lists all available parameters that can be accessed via `get` and `set` commands, indicating which are read-only.

**Usage:** `list`

#### `status`
Shows the current WiFi station connection status (idle, connecting, connected, failed, etc.).

**Usage:** `status`

### Network Commands

#### `scan`
Scans for available WiFi networks and displays them with:
- Network name (SSID)
- Signal strength (RSSI in dBm)
- Encryption status (Open/Encrypted)

**Usage:** `scan`

#### `connect`
Initiates WiFi connection setup using stored credentials.

**Usage:** `connect`

### Configuration Commands

#### `get <parameter>`
Retrieves the value of a configuration parameter. Use `list` to see available parameters.

**Usage:** `get Hostname`

**Example:**
```
> get Hostname
MyESP8266Device
```

#### `set <parameter> <value>`
Sets the value of a configuration parameter. Use `list` to see available parameters.

**Usage:** `set Hostname MyNewHostname`

**Example:**
```
> set Hostname MyNewDevice
Success
```

#### `save`
Saves current settings to flash memory.

**Usage:** `save`

#### `restore`
Restores settings from flash memory.

**Usage:** `restore`

### Utility Commands

#### `echo <text>`
Echoes the provided text back to the shell.

**Usage:** `echo Hello World`

#### `strlen <text>`
Displays the length of the provided text argument.

**Usage:** `strlen Hello`

**Example:**
```
> strlen Hello
Length = 5
```

#### `hex <number>`
Converts a decimal number to hexadecimal format.

**Usage:** `hex 255`

**Example:**
```
> hex 255
FF
```

#### `beep [count]`
Triggers a beep sound on the connected device. Optional count parameter (default: 1).

**Usage:** `beep` or `beep 3`

### Display Commands

#### `clear` / `cls`
Clears the terminal screen.

**Usage:** `clear` or `cls`

#### `upper`
Enables uppercase mode for command input display.

**Usage:** `upper`

#### `noupper`
Disables uppercase mode for command input display.

**Usage:** `noupper`

### System Commands

#### `reboot`
Reboots the ESP8266 device.

**Usage:** `reboot`

**Warning:** This will immediately restart the device.

#### `busy`
Sends a "busy" status signal to the connected PU device.

**Usage:** `busy`

#### `ready`
Sends a "ready" status signal to the connected PU device.

**Usage:** `ready`

#### `exit`
Exits the shell session.

**Usage:** `exit`

## Navigation and Editing

The shell supports VT100-style terminal navigation:
- **Arrow Keys**: Move cursor left/right, navigate command history (up/down)
- **Home/End**: Jump to beginning/end of line
- **Backspace/Delete**: Delete characters
- **Ctrl+C**: Cancel current command
- **Ctrl+L**: Clear screen and redraw prompt

## Parameters

Available parameters (use with `get` and `set` commands):

### Network Configuration
- `Hostname` - Device hostname
- `WiFi_Mode` - WiFi mode (Station/AP/Both)
- `Station_SSID` - Station mode SSID
- `Station_Password` - Station mode password
- `AP_SSID` - Access Point SSID
- `AP_Password` - Access Point password

### System Information (Read-Only)
- `Uptime` - System uptime
- `Firmware_Hash` - Current firmware MD5 hash
- `Build_Date` - Firmware build date and time

## Examples

### Check system status
```
> info
System Information:
  Chip ID: 0x123456
  Flash ID: 0xEF4016
  Flash Size: 4.00 MB
  ...
```

### Scan for WiFi networks
```
> scan
Scanning WiFi networks...
Found 5 networks:
  1: MyNetwork (-45 dBm) [Encrypted]
  2: GuestWiFi (-67 dBm) [Open]
  ...
```

### Configure WiFi
```
> set Station_SSID MyNetwork
Success
> set Station_Password MyPassword123
Success
> save
Saved
> connect
```

### Check memory usage
```
> mem
Memory Information:
  Free Heap: 25.5 KB
  Heap Fragmentation: 12%
  Max Free Block: 15.2 KB
  ...
```
