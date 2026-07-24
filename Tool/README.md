# Firmware transfer clients

This directory contains four maintained clients for the same product-neutral
firmware transfer protocol. They work with PU850, Rayan Lamp, or any compatible
firmware profile by changing the endpoint—not by rebuilding the client.

## Pick a client

| Client | Runtime and target | Upload | Download / resume | Watch | Best fit |
| --- | --- | :---: | :---: | :---: | --- |
| [Node](NODE-UPLOADER.md) | Node.js 20.19+; Windows, Linux, macOS | Yes | Yes | Yes, default | Reference behavior and build-directory automation |
| [.NET](C%23/README.md) | .NET 8; portable or self-contained | Yes | Yes | Yes | Managed deployment and PowerShell-oriented environments |
| [Go](Go/README.md) | Single CGO-free executable; Windows and Linux | Yes | Yes | Yes | Small portable cross-platform binary |
| [C++](C++/README.md) | Native Windows WinHTTP executable | Yes | Yes | Yes | Windows systems with no managed runtime |

All clients validate local firmware before an upload, require the device's exact
`ok!` acknowledgement, and verify the installed raw firmware hash after reboot.
The download-capable clients write through a `.part` file, validate advertised
hashes and lengths, and replace the requested destination only after successful
verification.

## Shared wire contract

The Arduino build produces a gzip-compressed `firmware.bin` and a strict
two-record `firmware.bin.md5` manifest:

- the plain record is the MD5 of the raw sketch;
- the `(compressed)` record is the MD5 of the gzip body;
- `POST /update` receives multipart fields named `MD5` and `firmware`;
- `/update/info`, with `/info` as a compatibility fallback, reports the running
  raw hash;
- `/firmware/download` serves the running raw image and supports strict byte
  ranges for resumable clients.

An update URL normally looks like `http://device.local/update`. The download
URL is derived from the same origin unless explicitly overridden. A configured
bearer token protects metadata, upload, and download requests; prefer each
client's environment-variable option so a token is not exposed in process
arguments. Never commit device credentials.

The tools pin a resolved `.local` address for a transfer and allow one fresh
resolution only after a safe connection failure. They do not blindly replay an
ambiguous upload.

## Validate every implementation

On Windows with Node.js, Go, .NET 8, Python 3, CMake, and a Visual Studio
Desktop C++ workload installed:

```powershell
pwsh -File .\Tool\build-all.ps1
```

The orchestrator restores dependencies, builds all four clients, and runs their
offline or loopback-only tests. It never targets a physical device. Generated
outputs stay under ignored `node_modules`, `bin`, `obj`, and `build`
directories. The script supports PowerShell 7 and Windows PowerShell 5.1.

Useful options:

```powershell
# Start from the orchestrator-owned build outputs.
pwsh -File .\Tool\build-all.ps1 -Clean

# Validate only selected implementations.
pwsh -File .\Tool\build-all.ps1 -Only Node,Go,CSharp

# Reuse dependencies already restored by CI or a previous run.
pwsh -File .\Tool\build-all.ps1 -SkipRestore
```

The C++ implementation is intentionally native-Windows and therefore runs in a
Windows CI job. Node, Go, and .NET validation run independently on Linux so a
failure identifies the affected implementation immediately.

## Protocol test boundary

The automated suites use temporary files and loopback HTTP servers. They may
open localhost ports, but they do not resolve a configured device name, access
the LAN, upload firmware, or invoke the firmware build. Real-device transfer
remains an explicit operator action.
