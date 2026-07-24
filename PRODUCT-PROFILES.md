# Product profiles and firmware transfer contract

This repository is the shared upstream for ASA-derived ESP8266 firmware. PU850
is the default product. Product forks should keep reusable networking, shell,
build, and transfer code aligned with this repository and carry only their
device-specific protocol and I/O logic.

## Compile-time identity

`ProductConfig.h` is the single source of default product identity used by
HTTP, WebSocket, SSDP/UPnP, MNDP, diagnostics, and firmware download metadata.
Every `FW_*` value may be supplied as a compiler definition or defined in the
generated/local header before `ProductConfig.h` is included.

The built-in profiles are:

| Profile | Numeric value | Intended use |
| --- | ---: | --- |
| `pu850` | `1` | PU850 indicator firmware (default) |
| `rayanlamp` | `2` | Rayan Lamp downstream identity |

Examples:

```console
build.cmd --product-profile pu850
build.cmd --product-profile rayanlamp
PRODUCT_PROFILE=rayanlamp ./build.sh
```

Individual values can be overridden without adding another profile:

```console
set EXTRA_BUILD_FLAGS=-DFW_PRODUCT_NAME=\"Lab Device\" -DFW_PRODUCT_MODEL_NAME=\"LAB-1\"
build.cmd
```

Do not commit credentials in a profile or pass them through compiler flags,
which can appear in process listings and build logs. If OTA authentication is
required, copy `~secrets.h.example` to the ignored `~secrets.h` and define
`FW_OTA_BEARER_TOKEN` there. Build summaries and metadata deliberately omit
compiler flags, and the build scripts reject this token in `EXTRA_BUILD_FLAGS`.

## Transfer endpoints

The shared transfer protocol is deliberately versionless:

| Endpoint | Purpose |
| --- | --- |
| `POST /update` | Multipart gzip OTA upload. `MD5` is the compressed body MD5. |
| `GET /update/info` | JSON product metadata and the running raw-sketch hash. |
| `GET /info` | Human-readable diagnostics and hash fallback. |
| `GET /firmware/download` | Running raw sketch with length and integrity metadata. |
| `HEAD /firmware/download` | Download metadata without a response body. |

`/firmware/download` supports one RFC 7233 byte range, conditional ETags, and
atomic resume in the supplied clients. A full-flash dump may expose Wi-Fi and
persisted application settings, so `?full=true` is rejected unless
`FW_ENABLE_FULL_FLASH_DOWNLOAD=1` is explicitly selected.

The build artifact remains gzip-compressed for OTA. Its adjacent `.md5` file
contains exactly two `md5sum` records: first the raw/gunzipped sketch, then the
compressed artifact marked with ` (compressed)`. Transfer clients validate both
records before network access, compare the raw hash before upload, and verify
the running raw hash after reboot.

## Downstream policy

A device fork should:

1. Track this repository as its `upstream` remote.
2. Rebase or merge shared firmware/tooling changes from `upstream/main`.
3. Select its product profile in the build rather than duplicating identity
   strings throughout the codebase.
4. Keep device-only routes, protocols, GPIO, and UPnP services in downstream
   modules.
5. Exclude unrelated PU850 UART, weight, receipt, report, page, and file
   operations when they are not part of that device.

Wi-Fi passwords are redacted from `/config/ap` and `/config/st` by default.
Legacy exposure requires the explicit `FW_EXPOSE_WIFI_CREDENTIALS=1` build flag.
