# PU850 ESP8266 API Reference

## Table of Contents

1. [Overview](#overview)
2. [Authentication](#authentication)
3. [WebSocket API](#websocket-api)
4. [REST API Endpoints](#rest-api-endpoints)
5. [Response Codes](#response-codes)
6. [Data Formats](#data-formats)
7. [Error Handling](#error-handling)
8. [Rate Limiting](#rate-limiting)
9. [Examples](#examples)

## Overview

### Base URL
```
http://[device-ip]/
```

### Content Types
- **Request**: `application/x-www-form-urlencoded`, `application/json`
- **Response**: `text/plain`, `application/json`, `application/octet-stream`

### HTTP Methods
- `GET`: Retrieve data
- `POST`: Create/modify data, execute actions
- `HEAD`: Get headers only (for file metadata)
- `OPTIONS`: CORS preflight

### CORS Support
All endpoints support Cross-Origin Resource Sharing (CORS):
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: *
Access-Control-Max-Age: 600
```

## Authentication

### Session-Based Authentication

#### Login
**Not fully implemented**. Currently, all endpoints are accessible without authentication.

Future implementation:
```http
POST /login
Content-Type: application/x-www-form-urlencoded

username=user&password=pass
```

Response:
```
HTTP/1.1 200 OK
Set-Cookie: TOKEN=32-character-hex-token; Path=/; HttpOnly

{
  "token": "32-character-hex-token",
  "accessLevel": 3,
  "expiresIn": 3600
}
```

#### Token Usage

**Authorization Header**:
```http
GET /endpoint
Authorization: Bearer 32-character-hex-token
```

**Cookie**:
```http
GET /endpoint
Cookie: TOKEN=32-character-hex-token
```

**Query Parameter**:
```http
GET /endpoint?token=32-character-hex-token
```

### Access Levels

```cpp
enum AccessLevel {
    UnknownLevel_ = 0,     // No access
    GuestLevel_   = 1,     // Read-only access
    UserLevel_    = 2,     // Standard user
    AdminLevel_   = 3,     // Administrator
    OwnerLevel_   = 4      // Full control
};
```

## WebSocket API

### Connection
```javascript
const ws = new WebSocket('ws://[device-ip]/ws');
```

### Client → Server Messages

| Message | Description |
|---------|-------------|
| `weight` | Request current weight |
| `datetime` | Request current date/time |
| `client:helloWorld` | Test message |

### Server → Client Messages

| Format | Description | Example |
|--------|-------------|---------|
| `[number]` | Weight value (grams) | `12345` |
| `off` | Weight unavailable | `off` |
| `status:[n]` | Device status | `status:1` |
| `host:[name]` | Hostname changed | `host:PU850` |
| `SN:[number]` | Serial number | `SN:123456` |
| `page:[n]` | Page ID | `page:5` |
| `user:[name]` | Username (URL encoded) | `user:admin` |
| `level:[n]` | Access level | `level:3` |
| `lang:[c]` | Language code | `lang:E` |
| `is_exec:[n]` | Is executable | `is_exec:1` |
| `[datetime]` | Date/time | `2024/12/09 15:30:45` |
| `hidden:[n]` | Weight display hidden | `hidden:0` |
| `tare:[code]` | Tare result | `tare:0` |
| `msg:[icon],[text]` | Message notification | `msg:5,overflow` |
| `beep:[count]` | Beep notification | `beep:2` |
| `update:[n]%` | OTA update progress | `update:50%` |
| `rebooting` | Device rebooting | `rebooting` |

## REST API Endpoints

### System Information

#### GET /info
Get comprehensive device information.

**Response** (text/plain):
```
hostname: PU850
wifi version: 2.7.4/WebServer:3.9.2
esp chip id: 123ABC
flash chip id: 1640E0
firmware hash: abc123def456...
protocol rev.: 1 (no match)
st mac: AA:BB:CC:DD:EE:FF
ap mac: AA:BB:CC:DD:EE:01
client ip: 192.168.1.100
flash size: 1.00 MB
program usage: 78%
settings hash: DEADBEEF, status: 0
heap memory: 25600
build: Dec  9 2024 12:00:00 by user@host
uptime: 1 days, 05:30:00
```

#### GET /update/info
Get firmware update information.

**Response** (application/json):
```json
{
  "hash": "abc123def456...",
  "size": 700000,
  "free": 300000,
  "date": "Dec  9 2024 12:00:00"
}
```

### Weight Operations

#### GET /weight/get
Get current weight value.

**Response** (text/plain):
```
12345
```
or
```
fail
```

**Weight value**: Integer in grams (or configured unit)

---

#### POST /action/tare
Zero the scale (tare operation).

**Response** (text/plain):
```
ok!
```
or
```
fail
```

---

#### GET /weight/display
Check if weight display is hidden.

**Response** (text/plain):
```
shown
```
or
```
hidden
```
or
```
unknown
```

---

#### POST /weight/display
Show or hide weight on PU850 display.

**Parameters**:
- `value`: `show` or `hide`

**Request**:
```http
POST /weight/display?value=hide
```

**Response** (text/plain):
```
ok!
```
or
```
fail
```
or
```
invalid
```

### Power Operations

#### GET /power
Get power and battery status.

**Response** (text/plain):
```
1,85
```

Format: `[power_status],[battery_percentage]`

Power status:
- `0`: External power
- `1`: Battery power
- `2`: Low battery
- `255`: Unknown

### Date and Time

#### GET /datetime/get
Get current date and time from PU850.

**Response** (text/plain):
```
2024/12/09 15:30:45
```

Format: `YYYY/MM/DD HH:MM:SS`

---

#### POST /datetime/set
Set date and time on PU850.

**Parameters**:
- `val`: Date/time string (URL encoded)

**Request**:
```http
POST /datetime/set?val=2024%2F12%2F09%2015%3A30%3A45
```

**Response** (text/plain):
```
ok!
```
or
```
fail
```

### File Operations

#### GET /receipt
Download receipt by ID.

**Parameters**:
- `id`: Receipt ID (integer)

**Request**:
```http
GET /receipt?id=12345
```

**Response** (application/octet-stream):
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="receipt.txt"

[Binary receipt data]
```

**Error Responses**:
- `400 Bad Request`: Invalid ID
- `404 Not Found`: Receipt not found
- `403 Forbidden`: Access denied
- `500 Internal Server Error`: Operation failed
- `503 Service Unavailable`: Device busy

---

#### GET /file/download
Download file from PU850 storage.

**Parameters**:
- `filename`: File name

**Request**:
```http
GET /file/download?filename=report.txt
```

**Response** (application/octet-stream):
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="report.txt"
Content-Length: 1024
Accept-Ranges: bytes
Last-Modified: Mon, 09 Dec 2024 15:30:45 GMT

[File data]
```

**Range Requests**:
```http
GET /file/download?filename=large_file.bin
Range: bytes=0-1023
```

**Response**:
```
HTTP/1.1 206 Partial Content
Content-Range: bytes 0-1023/10240
Content-Length: 1024

[Partial file data]
```

---

#### HEAD /file/download
Get file metadata without downloading.

**Request**:
```http
HEAD /file/download?filename=report.txt
```

**Response**:
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Length: 1024
Last-Modified: Mon, 09 Dec 2024 15:30:45 GMT
```

---

#### GET /file/cancel
Cancel ongoing file transfer.

**Response** (text/plain):
```
canceled
```

### System Control

#### POST /pu_reboot
Reboot PU850 main unit (software reset).

**Response** (text/plain):
```
ok!
```
or
```
fail
```
or
```
forbidden
```

---

#### POST /pu_reset
Hardware reset of PU850 main unit.

**Response** (text/plain):
```
ok!
```

---

#### POST /message/send
Send text message to PU850 display.

**Parameters**:
- `text`: Message text

**Request**:
```http
POST /message/send?text=Hello%20World
```

**Response** (text/plain):
```
ok!
```
or
```
fail
```

---

#### GET /beep
Trigger buzzer beep.

**Parameters**:
- `count`: Number of beeps (default: 1)

**Request**:
```http
GET /beep?count=3
```

**Response** (text/plain):
```
beep
```

### Configuration

#### GET /username
Get current logged-in user.

**Response** (text/plain):
```
admin
3
1
```

Format:
```
[username or --]
[access_level]
[is_executable]
```

---

#### GET /lang
Get system language.

**Response** (text/plain):
```
E
```

Language codes:
- `E`: English
- `F`: French
- `A`: Arabic
- `R`: Russian/Farsi

---

#### POST /lang
Set system language.

**Parameters**:
- `lang`: Language code (E/F/A/R)

**Request**:
```http
POST /lang?lang=E
```

**Response** (text/plain):
```
ok!
```
or
```
fail
```

---

#### GET /page_id
Get current page/screen ID on PU850.

**Response** (text/plain):
```
5
```
or
```
fail
```

---

#### POST /page_goto
Navigate to specific page on PU850.

**Parameters**:
- `pu_id`: Page ID

**Request**:
```http
POST /page_goto?pu_id=5
```

**Response** (text/plain):
```
ok!
```
or
```
fail
```

---

#### GET /pu
Get PU850 status and information.

**Response** (text/plain):
```
Ready
123456
2.1.0
```

Format:
```
[status: Ready/Busy/Booting/Unknown]
[serial_number or fail]
[main_version]
```

---

#### GET /is_exec
Check if current user can execute operations.

**Response** (text/plain):
```
1
```
or
```
0
```
or
```
fail
```

### Network Information

#### GET /wifi/signal
Get WiFi signal strength and quality.

**Response** (text/plain):
```
-55
Normal
```

Format:
```
[RSSI in dBm]
[Quality: Excellent/Good/Normal/Low/Bad/No signal]
```

---

#### GET /wifi/status
Get WiFi connection status.

**Response** (text/plain):
```
ap status: 1
st status: 3
```

Status codes:
- `0`: Idle
- `1`: Connecting
- `2`: Wrong password
- `3`: Not found
- `4`: Connect fail
- `5`: Disconnected
- `6`: OK/Connected

---

#### GET /wifi/country
Get WiFi country code.

**Response** (text/plain):
```
country: US
```

---

#### GET /config/ap
Get Access Point configuration.

**Response** (text/plain):
```
ssid: PU850-AP
pass: password123
ip: 192.168.4.1
netmask: 255.255.255.0
```

---

#### GET /config/st
Get Station (client) configuration.

**Response** (text/plain):
```
ssid: MyWiFi
pass: wifipassword
dhcp: enabled
ip: 192.168.1.100
netmask: 255.255.255.0
gw: 192.168.1.1
dns: 8.8.8.8, 8.8.4.4
```

---

#### POST /hostname/check
Resolve hostname to IP address.

**Parameters**:
- `value`: Hostname to check

**Request**:
```http
POST /hostname/check?value=google.com
```

**Response** (text/plain):
```
8.8.8.8
```
or
```
hostname not found
```

### Firmware Management

#### GET /update
OTA update web interface (HTML form).

#### POST /update
Upload new firmware.

**Request**:
```http
POST /update
Content-Type: multipart/form-data

[Firmware binary data]
```

**Response**:
```
HTTP/1.1 200 OK

Update successful, rebooting...
```

#### GET /firmware/download
Download current firmware from device.

**Parameters**:
- `full`: Set to `true` to download entire flash (optional)

**Request**:
```http
GET /firmware/download
```

**Response** (application/octet-stream):
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="firmware.bin"
Content-MD5: [base64-encoded-md5]
Accept-Ranges: bytes

[Firmware binary]
```

**Range requests supported** for resumable downloads.

### Debug Endpoints

These endpoints are only available when compiled with `--debug-tools`:

#### GET /debug/info
Get debug buffer information.

**Response** (text/plain):
```
Used Size: 1024
Total Size: 8192
Last Reset: External System Reset
```

---

#### GET /debug/data
View debug buffer contents.

**Response** (text/plain):
```
[Debug log data]
```

---

#### POST /debug/clear
Clear debug buffer.

**Response** (text/plain):
```
cleared
```

---

#### GET /eProcessState
Get UART protocol state machine status.

**Response** (text/plain):
```
eProcessState = 0
MasterStatus = 1
```

---

#### GET /eResponseStatus
Get last response status from UART.

**Response** (text/plain):
```
eResponseSuffix = 10
eResponseCode = 0
```

---

#### GET /datetime/parse
Parse date/time string (debugging).

**Parameters**:
- `val`: Date/time string

**Request**:
```http
POST /datetime/parse?val=2024%2F12%2F09%2015%3A30%3A45
```

**Response** (text/plain):
```
2024/12/09 15:30:45
epoch: 1702135845
```

---

#### GET /datetime/active
Get active/adjusted date/time.

**Response** (text/plain):
```
2024/12/09 15:30:45
epoch: 1702135845
```

---

#### GET /auth_dump
Dump authentication attributes (debugging).

**Response** (text/plain):
```
username: admin
password: ****
token: abc123...
session_id: 0
```

---

#### GET /sessions_dump
Dump all active sessions.

**Response** (text/plain):
```
Token: abc123def456...
Level: 3
Is Logged In: yes
Last Active: 1702135845
----
```

---

#### GET /random
Generate random hex string (testing).

**Response** (text/plain):
```
A1B2C3D4E5F6...
```

---

#### GET /crash
Intentionally crash the device (testing crash handler).

**No response** (device crashes and reboots)

## Response Codes

### HTTP Status Codes

| Code | Description |
|------|-------------|
| 200 | OK - Request successful |
| 206 | Partial Content - Range request |
| 400 | Bad Request - Invalid parameters |
| 403 | Forbidden - Access denied |
| 404 | Not Found - Resource not found |
| 416 | Range Not Satisfiable - Invalid range |
| 500 | Internal Server Error - Operation failed |
| 503 | Service Unavailable - Device busy |
| 504 | Gateway Timeout - No response from PU850 |

### Operation Result Codes

Used in UART protocol responses:

```cpp
#define result_Success        0x00  // Operation succeeded
#define result_Fail           0x01  // Operation failed
#define result_Not_Found      0x02  // Resource not found
#define result_Unauthorised   0x03  // Access denied
#define result_NotAcceptable  0x04  // Invalid request
#define result_Processing     0x05  // Still processing
#define result_Undefined      0xFF  // Unknown/timeout
```

## Data Formats

### Date/Time Format
```
YYYY/MM/DD HH:MM:SS
```

Example: `2024/12/09 15:30:45`

### Weight Format
Integer value in grams (or configured unit)
```
12345
```

### IP Address Format
Standard dotted decimal notation
```
192.168.1.100
```

### MAC Address Format
Colon-separated hexadecimal
```
AA:BB:CC:DD:EE:FF
```

### Hostname Format
RFC952 compliant:
- Start with letter
- Letters, numbers, hyphens only
- Max 24 characters
- No trailing hyphen

## Error Handling

### Error Response Format

**Text Response**:
```
fail
```
or
```
invalid
```
or
```
result: [code]
```

**JSON Response** (future):
```json
{
  "error": "error_code",
  "message": "Human readable error message",
  "details": {
    "field": "parameter_name",
    "reason": "validation_failed"
  }
}
```

### Common Error Scenarios

#### Device Busy
```
HTTP/1.1 503 Service Unavailable
Content-Type: text/plain

device is busy
```

#### Invalid Parameter
```
HTTP/1.1 400 Bad Request
Content-Type: text/plain

missing `param` argument
```

#### Operation Failed
```
HTTP/1.1 500 Internal Server Error
Content-Type: text/plain

fail
```

#### Access Denied
```
HTTP/1.1 403 Forbidden
Content-Type: text/plain

forbidden
```

## Rate Limiting

Currently, no rate limiting is implemented. However:

- Keep WebSocket connections to reasonable number (max 10)
- Limit file download requests to avoid memory exhaustion
- Space bulk operations (tare, reboot) appropriately
- Don't poll rapidly (suggested: max 1 request/second per endpoint)

## Examples

### JavaScript (Browser)

#### Fetch API
```javascript
// Get weight
fetch('http://device-ip/weight/get')
  .then(response => response.text())
  .then(weight => console.log('Weight:', weight));

// Tare scale
fetch('http://device-ip/action/tare', { method: 'POST' })
  .then(response => response.text())
  .then(result => console.log('Tare result:', result));

// Download file
fetch('http://device-ip/file/download?filename=report.txt')
  .then(response => response.blob())
  .then(blob => {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'report.txt';
    a.click();
  });
```

#### WebSocket
```javascript
const ws = new WebSocket('ws://device-ip/ws');

ws.onopen = () => {
  console.log('Connected');
  ws.send('weight'); // Request weight
};

ws.onmessage = (event) => {
  const data = event.data;
  
  if (data === 'off') {
    console.log('Weight unavailable');
  } else if (data.match(/^\d+$/)) {
    console.log('Weight:', parseInt(data));
  } else if (data.startsWith('status:')) {
    const status = parseInt(data.split(':')[1]);
    console.log('Status:', status);
  } else if (data.startsWith('msg:')) {
    const [icon, message] = data.substring(4).split(',');
    console.log('Message:', icon, message);
  }
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};

ws.onclose = () => {
  console.log('Disconnected');
};
```

### Python

```python
import requests
import websocket

# Get weight
response = requests.get('http://device-ip/weight/get')
weight = response.text
print(f'Weight: {weight}')

# Tare scale
response = requests.post('http://device-ip/action/tare')
print(f'Tare result: {response.text}')

# Set date/time
from datetime import datetime
dt = datetime.now().strftime('%Y/%m/%d %H:%M:%S')
response = requests.post(
    'http://device-ip/datetime/set',
    params={'val': dt}
)
print(f'Set datetime result: {response.text}')

# Download file
response = requests.get(
    'http://device-ip/file/download',
    params={'filename': 'report.txt'},
    stream=True
)
with open('report.txt', 'wb') as f:
    for chunk in response.iter_content(chunk_size=8192):
        f.write(chunk)

# WebSocket
def on_message(ws, message):
    print(f'Received: {message}')

def on_error(ws, error):
    print(f'Error: {error}')

def on_close(ws, close_status_code, close_msg):
    print('Connection closed')

def on_open(ws):
    print('Connected')
    ws.send('weight')

ws = websocket.WebSocketApp(
    'ws://device-ip/ws',
    on_open=on_open,
    on_message=on_message,
    on_error=on_error,
    on_close=on_close
)
ws.run_forever()
```

### curl

```bash
# Get weight
curl http://device-ip/weight/get

# Tare scale
curl -X POST http://device-ip/action/tare

# Set language
curl -X POST "http://device-ip/lang?lang=E"

# Download file
curl -o report.txt "http://device-ip/file/download?filename=report.txt"

# Download with resume support
curl -C - -o firmware.bin "http://device-ip/firmware/download"

# Upload firmware
curl -F "update=@firmware.bin" http://device-ip/update

# Check WiFi signal
curl http://device-ip/wifi/signal

# Get system info
curl http://device-ip/info
```

### Node.js

```javascript
const axios = require('axios');
const WebSocket = require('ws');

// Get weight
async function getWeight() {
  try {
    const response = await axios.get('http://device-ip/weight/get');
    console.log('Weight:', response.data);
  } catch (error) {
    console.error('Error:', error.message);
  }
}

// Tare scale
async function tare() {
  try {
    const response = await axios.post('http://device-ip/action/tare');
    console.log('Tare result:', response.data);
  } catch (error) {
    console.error('Error:', error.message);
  }
}

// WebSocket connection
const ws = new WebSocket('ws://device-ip/ws');

ws.on('open', () => {
  console.log('Connected');
  ws.send('weight');
});

ws.on('message', (data) => {
  console.log('Received:', data.toString());
});

ws.on('error', (error) => {
  console.error('WebSocket error:', error);
});

ws.on('close', () => {
  console.log('Disconnected');
});

// Download file
const fs = require('fs');

async function downloadFile(filename) {
  const response = await axios.get(
    `http://device-ip/file/download?filename=${filename}`,
    { responseType: 'stream' }
  );
  
  const writer = fs.createWriteStream(filename);
  response.data.pipe(writer);
  
  return new Promise((resolve, reject) => {
    writer.on('finish', resolve);
    writer.on('error', reject);
  });
}
```

---

## Support

For API questions or issues:
- Review the [User Guide](USER_GUIDE.md)
- Check the [Developer Guide](DEVELOPER_GUIDE.md)
- Contact: David@Refoua.me
