# ASA Firmware Transfer for Go

`ASAFirmwareTransfer` is the standard-library-only, CGO-free firmware transfer
tool for ASA-family ESP firmware. It supports strict OTA upload, verified
firmware download, pre-upload backup, validation-only runs, and content-stable
watch mode.

## Upload

The tool requires the exact two entries emitted in `firmware.bin.md5`, validates
the bounded gzip stream, recalculates both compressed and gunzipped raw MD5
values, and checks that the manifest names the selected file. It then reads
`/update/info` (falling back to `/info`) and skips an unnecessary upload when
the device already reports the manifest's raw firmware MD5.

```powershell
$env:ASA_FIRMWARE_API = 'http://device.local/update'
.\bin\win-x64\ASAFirmwareTransfer.exe firmware.bin

# Upload even when the preflight hash already matches
.\bin\win-x64\ASAFirmwareTransfer.exe -force firmware.bin

# Atomically download and verify the running firmware before uploading
.\bin\win-x64\ASAFirmwareTransfer.exe `
  -backup .\backups\before.bin firmware.bin
```

The ESP-compatible multipart request contains the `MD5` and `firmware` fields
and must receive the exact body `ok!`. A successful HTTP response is not enough:
every upload must subsequently report the expected raw firmware MD5 through
`/update/info` or `/info` before the command succeeds.

## Download

The download endpoint defaults to `/firmware/download` on the `-api` origin.
Use `-download-url` for a custom endpoint.

```powershell
.\bin\win-x64\ASAFirmwareTransfer.exe `
  -api http://device.local/update `
  -download .\backups\running.bin

.\bin\win-x64\ASAFirmwareTransfer.exe `
  -download .\backups\running.bin `
  -download-url http://device.local/firmware/download

# Continue an existing running.bin.part with one bounded Range request
.\bin\win-x64\ASAFirmwareTransfer.exe `
  -api http://device.local/update `
  -download .\backups\running.bin -resume
```

Downloads require an exact `Content-Length`, are bounded to 64 MiB, and verify
every applicable `Content-MD5`, `X-Firmware-MD5`, and MD5-shaped `ETag`.
`/update/info` is preferred for the full raw hash and size, with plain or JSON
`/info` as a fallback. The destination is untouched until `OUTPUT.part` passes
all available full-file checks and is atomically moved into place. A failed
download retains the `.part` file so an operator can inspect or explicitly
resume it.

Resume sends at most one successful transfer request, requires a precise HTTP
206 `Content-Range`, and verifies the completed file rather than trusting the
partial response alone.

## Validation and watch mode

```powershell
# No network traffic
.\bin\win-x64\ASAFirmwareTransfer.exe -check firmware.bin

# Upload one stable build, then monitor content-stable rebuilds
.\bin\win-x64\ASAFirmwareTransfer.exe `
  -api http://device.local/update -watch firmware.bin
```

Watch mode compares file content rather than timestamps alone. Both the firmware
and manifest must remain unchanged for one second. It pauses while
`firmware.bin.gz` or the sibling `~local.h` marker exists, tolerates temporary
deletion/recreation, serializes uploads, coalesces changes during a transfer, and
does not endlessly replay a failed snapshot.

## Authentication and endpoint behavior

Set `UPDATE_BEARER_TOKEN` (or `UPDATE_TOKEN`) to add an `Authorization: Bearer`
header. `ASA_FIRMWARE_BEARER_TOKEN` and `FIRMWARE_BEARER_TOKEN` remain
compatible. `-bearer` is available for controlled scripting, but an environment
variable avoids exposing a token in process listings. Tokens are never printed.
Redirects are refused so credentials and firmware cannot be forwarded to another
endpoint.

`ASA_FIRMWARE_API` is preferred; existing automation using `UPDATE_API` remains
compatible. A hostname is resolved once and pinned for the process. A failed
cached connection permits one fresh resolution. Device-supplied terminal text
is control-character sanitized.

Stable process exit codes are:

- `0`: requested operation completed and verification passed
- `1`: transfer, device response, cancellation, or verification failure
- `2`: invalid arguments, endpoint, or local configuration

## Build and test

```powershell
.\build.ps1
```

From Linux or another POSIX shell, run `sh ./build.sh`. Both scripts run the
ten-pass test suite and `go vet`, force `CGO_ENABLED=0`, cross-build Windows
x64/x86 and Linux x64, and print SHA-256 hashes. Pass `-SkipTests` to PowerShell
or `--skip-tests` to the shell script only when validation already ran.

The test suite uses deterministic local HTTP servers. It never contacts a
device and covers multipart upload, mandatory post-hash verification,
skip-identical preflight, verified backup ordering, header verification,
fallback metadata, atomic replacement, and exact one-request Range resume.
