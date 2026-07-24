# ASA Firmware Transfer for .NET

`AsaFirmwareTransfer` is the strict, dependency-free .NET 8 firmware transfer
client for ASA devices. It supports verified OTA uploads, continuous build
watching, and atomic verified downloads from the running device.

The executable is product-neutral. Device identity, hostnames, and credentials
come from its arguments or environment rather than being compiled into it.

## Protocol behavior

Uploads:

- validate the compressed image and raw-image entries in the Arduino `md5sum`
  manifest before opening a network connection;
- query `/update/info`, falling back to `/info`, as a preflight;
- optionally make a verified backup or skip an already-installed raw hash;
- stream the exact multipart fields `MD5` and `firmware`;
- require the exact success body `ok!`;
- poll `/update/info` with `/info` fallback after reboot and require the raw
  firmware hash by default;
- cache a `.local` address and perform at most one safe re-resolution after a
  connection failure.

Downloads:

- derive `/firmware/download` from the update endpoint unless
  `--download-url` is supplied;
- use HEAD before one GET and require a positive `Content-Length`;
- validate all available `Content-MD5`, `X-Firmware-MD5`, and MD5-shaped ETag
  values, rejecting conflicting headers;
- compare the downloaded image against `/update/info` or `/info` by default;
- write to `<destination>.part` and replace the destination only after every
  check succeeds;
- optionally resume an existing partial file with exactly one strict Range
  request, requiring HTTP 206 and an exact `Content-Range`.

Bearer and custom headers are supported without printing their values. Terminal
text received from a device is rendered inert, and ANSI styling is emitted only
to the corresponding interactive output stream.

## Usage

```powershell
# Backward-compatible one-shot upload
.\AsaFirmwareTransfer.exe firmware.bin --url http://asa-device.local/update

# Explicit upload with a verified pre-update backup
.\AsaFirmwareTransfer.exe upload firmware.bin `
  --url http://asa-device.local/update `
  --backup installed-firmware.bin `
  --skip-identical

# Atomic, remotely verified download
.\AsaFirmwareTransfer.exe download installed-firmware.bin `
  --url http://asa-device.local/update

# Resume installed-firmware.bin.part with one strict Range request
.\AsaFirmwareTransfer.exe download installed-firmware.bin `
  --url http://asa-device.local/update `
  --resume

# Private endpoint
.\AsaFirmwareTransfer.exe download installed-firmware.bin `
  --url https://device.example/update `
  --bearer $env:ASA_TOKEN `
  --header "X-Site: factory-a"
```

`UPDATE_API` may provide the update URL. `UPDATE_BEARER_TOKEN` is the preferred
bearer-token variable; `UPDATE_TOKEN`, `ASA_FIRMWARE_BEARER`,
`ASA_FIRMWARE_BEARER_TOKEN`, and `FIRMWARE_BEARER_TOKEN` remain compatible.
CLI values take precedence. Run `--help` for the complete, stable exit-code
table.

`--no-verify` is an explicit escape hatch. Normal uploads and downloads require
device-side hash verification.

### Continuous builds

```powershell
.\AsaFirmwareTransfer.exe firmware.bin `
  --url http://asa-device.local/update `
  --watch
```

Watch mode combines filesystem notifications with signature polling. It waits
until the firmware and manifest are stable, respects the Arduino build markers
`~local.h` and `<firmware>.gz`, coalesces changes during an upload, and skips a
byte-identical rebuild. The first Ctrl+C drains an in-flight update; the second
cancels it.

## Build, test, and publish

```powershell
.\build.ps1 -Clean
```

This performs a warning-as-error Release build, runs deterministic mock-device
self-tests, and publishes:

- `Uploader/publish/win-x64/AsaFirmwareTransfer.exe`
- `Uploader/publish/portable/AsaFirmwareTransfer.dll`
- `Uploader/publish/linux-x64/AsaFirmwareTransfer`

It also writes `Uploader/publish/SHA256SUMS.txt`.

The program uses only the .NET base class library. Self-tests never contact a
real device.
