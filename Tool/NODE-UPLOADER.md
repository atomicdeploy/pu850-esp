# Node firmware transfer tool

`upload.js` is the product-neutral reference firmware upload/download client
for ASA-derived devices. It validates both forms of the build before contacting
a device:

- `firmware.bin` is the gzip-compressed OTA body.
- `firmware.bin.md5` must contain exactly two standard `md5sum` records, one
  for the gunzipped/raw sketch and one named `firmware.bin (compressed)`.
- The multipart `MD5` field is the compressed digest expected by the ESP OTA
  receiver.
- `/update/info` (with `/info` fallback) and post-reboot verification use the
  raw sketch digest.

## Install and verify the tool

```console
npm ci
npm run check
```

The tests use loopback HTTP servers and never contact a physical device.

## Upload modes

Watching remains the default for compatibility:

```console
set UPDATE_API=http://device.local/update
node upload.js Build/ASA0002E.ino.bin
```

Each stable change to either the firmware or manifest triggers a transfer. An
identical target is skipped. A single upload is:

```console
node upload.js --immediate Build/ASA0002E.ino.bin
```

Useful upload options:

```console
node upload.js --check Build/ASA0002E.ino.bin
node upload.js --immediate --force Build/ASA0002E.ino.bin
node upload.js --immediate --backup backups/before.bin Build/ASA0002E.ino.bin
node upload.js --immediate --no-verify Build/ASA0002E.ino.bin
```

`--check` is completely offline. `--force` bypasses only the identical-target
optimization. Post-reboot raw-hash verification is mandatory unless
`--no-verify` is explicitly supplied. An upload response must be exactly
`ok!`; whitespace or additional text is a failure.

## Verified firmware download

```console
node upload.js --download backups/current.bin
```

The client obtains `/update/info`, checks `/firmware/download` metadata, and
validates `Content-Length` plus every available `X-Firmware-MD5`,
`Content-MD5`, and MD5-shaped `ETag`. Data is written to
`current.bin.part` and renamed only after verification. A bounded
`Range: bytes=N-` request resumes a valid partial file. Use `--no-resume` to
restart it.

## Authentication

Use one of:

```console
node upload.js --bearer TOKEN --immediate firmware.bin
node upload.js --authorization "Basic BASE64" --immediate firmware.bin
```

Environment equivalents are `UPDATE_BEARER_TOKEN` (or `UPDATE_TOKEN`) and
`UPDATE_AUTHORIZATION`. Tokens and Authorization values are never printed.
Credentials embedded in the URL are rejected.

## Address resolution and timeouts

The configured hostname is resolved once per transfer. The original `Host`
header and TLS server name are preserved while connecting to the pinned
address. At most one DNS refresh is allowed, and only after a safe network
failure before an upload body is committed or while making bodyless
metadata/reboot-poll requests.

Defaults are a 10-second request timeout, 30-second post-reboot deadline,
750-millisecond polling interval, and 16 MiB firmware/download safety limits.
They can be changed with `--timeout`, `--verify-timeout`,
`--verify-interval`, `--max-firmware-size`, and `--max-download-size`.

## Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | Success, verified download, offline check, or identical-target no-op |
| 2 | CLI/configuration error |
| 3 | Missing, malformed, or unusable local firmware/manifest |
| 4 | DNS, connection, or request timeout failure |
| 5 | Device HTTP/protocol/metadata failure |
| 6 | Local or downloaded content integrity failure |
| 7 | Upload rejected or response was not exactly `ok!` |
| 8 | Mandatory post-reboot hash verification failed |
| 9 | Local download/output filesystem failure |
| 130 | Interrupted |

Run `node upload.js --help` for the complete option list.
