# Native C++ firmware transfer tool

`FirmwareTransferCpp.exe` is a product-neutral, dependency-free Windows client
for the firmware HTTP contract. It uses WinHTTP, BCrypt, and Winsock, and links
the small zlib-licensed `puff` inflater into the executable for bounded gzip
validation.

## Build and test

From PowerShell with Visual Studio 2022:

```powershell
.\build.ps1
```

This compiles and runs the offline/loopback tests for both architectures, then
publishes static-runtime executables:

- `dist\x64\FirmwareTransferCpp.exe`
- `dist\Win32\FirmwareTransferCpp.exe`

For a faster x64-only pass:

```powershell
.\build.ps1 -Architecture x64 -Clean
```

The CMake tests never contact a physical device. They cover strict gzip and
manifest validation, authenticated preflight, identical-image skip and force,
verified backup, exact `ok!`, bounded post-reboot verification, `/update/info`
fallback to `/info`, and atomic download/resume with conflicting/corrupt hash
rejection.

### Cross-build the Windows executable from Linux

Install CMake, Ninja, and MinGW-w64:

```sh
chmod +x build.sh
./build.sh --architecture all --clean
```

Use `--architecture x64` or `--architecture x86` for one target. The results
are still Windows-native executables; the Windows build and CI job execute the
self-tests and loopback integration suite.

## Upload

The input is the build-produced gzip stream (normally still named `.bin`) plus a
strict two-record md5sum manifest:

```text
<raw-md5> *firmware.bin
<gzip-md5> *firmware.bin (compressed)
```

The tool inflates the gzip into bounded memory, verifies its CRC32 and size, and
computes both hashes itself before any device contact.

```powershell
.\dist\x64\FirmwareTransferCpp.exe `
  --endpoint http://device.local/update `
  C:\path\to\firmware.bin
```

One-shot upload is the C++ default. `UPDATE_API` can supply the URL. The upload
flow is:

1. Resolve the hostname once and retain the original `Host` header.
2. Read `/update/info`, falling back to legacy plain `/info`.
3. Skip an already-installed raw hash unless `--force` is present.
4. Optionally create a verified backup with `--backup PATH`.
5. Send multipart fields `MD5` (gzip hash) and `firmware` (exact gzip bytes).
6. Require HTTP 2xx and the exact three-byte acknowledgement `ok!`.
7. Poll the information endpoints until the raw hash matches, within a hard
   deadline. `--no-verify` is an explicit opt-out.

A cached address may be resolved one additional time after a safe, pre-body
network failure. A request is never automatically replayed after firmware bytes
may have been committed. Redirects are rejected.

## Download and backup

```powershell
.\dist\x64\FirmwareTransferCpp.exe `
  --download C:\backup\firmware.bin `
  --endpoint http://device.local/update
```

Downloads are written to `PATH.part`. If that regular file already contains a
prefix, the client requests exactly `bytes=<size>-` and accepts only the matching
single `Content-Range`; `--no-resume` restarts from byte zero. It validates:

- `/update/info` or `/info` raw MD5;
- `Content-Length` for `HEAD` and `GET`;
- any `X-Firmware-MD5`, `Content-MD5`, `Digest`, and `ETag` values;
- final file size and independently calculated MD5.

Only a fully verified file is atomically moved into place. Integrity failures
remove the poisoned partial file.

## Authentication

Every information, download, and upload request carries the same optional
Authorization value:

```powershell
.\FirmwareTransferCpp.exe --bearer $env:DEVICE_TOKEN firmware.bin
```

Supported sources are `--authorization`, `--bearer`,
`UPDATE_AUTHORIZATION`, `UPDATE_BEARER_TOKEN`, and `UPDATE_TOKEN`. Conflicting
sources are rejected and credentials are never printed.

## Offline, watch, and exit codes

```powershell
.\FirmwareTransferCpp.exe --check C:\path\to\firmware.bin
.\FirmwareTransferCpp.exe --self-test
.\FirmwareTransferCpp.exe --watch --endpoint http://device.local/update firmware.bin
```

Watch mode waits for the firmware and manifest to remain unchanged for the
debounce interval and does not replay an ambiguous attempt. It pauses while
`firmware.bin.gz` or sibling `~local.h` indicates an active build.

Exit codes are stable: `0` success/no-op, `2` usage, `3` local input/manifest,
`4` network, `5` device protocol, `6` integrity, `7` upload rejected, `8`
post-verify, `9` local I/O/internal, and `130` interrupted.

Run `FirmwareTransferCpp.exe --help` for all timeouts and safety limits.

## Vendored inflater

`puff.c` and `puff.h` are Mark Adler's zlib `contrib/puff` version 2.3 from
zlib 1.3.1. Its permissive notice is retained verbatim in `puff.h`.
