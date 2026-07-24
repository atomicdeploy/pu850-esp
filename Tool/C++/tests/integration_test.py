#!/usr/bin/env python3
"""Loopback firmware-transfer tests; this never contacts a physical device."""

from __future__ import annotations

import base64
import gzip
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


class MockServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        *,
        current_firmware: bytes,
        uploaded_hash: str,
        expected_upload: bytes | None = None,
        secret: str = "loopback-secret",
        upload_response: bytes = b"ok!",
        update_info_available: bool = True,
        corrupt_download: bool = False,
        accept_upload_hash: bool = True,
    ):
        super().__init__(("127.0.0.1", 0), MockHandler)
        self.current_firmware = current_firmware
        self.current_hash = md5(current_firmware)
        self.uploaded_hash = uploaded_hash
        self.expected_upload = expected_upload
        self.secret = secret
        self.upload_response = upload_response
        self.update_info_available = update_info_available
        self.corrupt_download = corrupt_download
        self.accept_upload_hash = accept_upload_hash
        self.upload_count = 0
        self.range_headers: list[str | None] = []
        self.authorizations: list[str | None] = []
        self.failure: BaseException | None = None


class MockHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    @property
    def mock(self) -> MockServer:
        return self.server  # type: ignore[return-value]

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def authenticated(self) -> bool:
        assert self.headers["Host"] == f"localhost:{self.mock.server_port}"
        authorization = self.headers.get("Authorization")
        self.mock.authorizations.append(authorization)
        if authorization == f"Bearer {self.mock.secret}":
            return True
        self.reply(401, b"unauthorized", {"WWW-Authenticate": "Bearer"})
        return False

    def reply(
        self,
        status: int,
        body: bytes,
        headers: dict[str, str] | None = None,
        *,
        head: bool = False,
    ) -> None:
        self.send_response(status)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if not head:
            self.wfile.write(body)
            self.wfile.flush()
        self.close_connection = True

    def firmware_headers(self, firmware: bytes) -> dict[str, str]:
        digest = bytes.fromhex(md5(firmware))
        encoded = base64.b64encode(digest).decode("ascii")
        return {
            "Accept-Ranges": "bytes",
            "Content-Type": "application/octet-stream",
            "X-Firmware-MD5": md5(firmware),
            "Content-MD5": encoded,
            "Digest": f"md5={encoded}",
            "ETag": f'"{md5(firmware)}"',
        }

    def send_info(self, *, json: bool) -> None:
        if json:
            body = (
                '{"product":"loopback","hash":"'
                + self.mock.current_hash
                + '","size":123}'
            ).encode("ascii")
            self.reply(200, body, {"Content-Type": "application/json"})
        else:
            self.reply(
                200,
                (
                    "hostname: loopback\n"
                    f"firmware hash: {self.mock.current_hash}\n"
                    "build: integration-test\n"
                ).encode("ascii"),
                {"Content-Type": "text/plain"},
            )

    def do_HEAD(self) -> None:
        try:
            if not self.authenticated():
                return
            assert self.path == "/firmware/download"
            firmware = self.mock.current_firmware
            self.reply(
                200,
                firmware,
                self.firmware_headers(firmware),
                head=True,
            )
        except BaseException as error:
            self.mock.failure = error
            raise

    def do_GET(self) -> None:
        try:
            if not self.authenticated():
                return
            if self.path == "/update/info":
                if not self.mock.update_info_available:
                    self.reply(404, b"not found")
                else:
                    self.send_info(json=True)
                return
            if self.path == "/info":
                self.send_info(json=False)
                return
            assert self.path == "/firmware/download"
            firmware = self.mock.current_firmware
            range_header = self.headers.get("Range")
            self.mock.range_headers.append(range_header)
            status = 200
            body = firmware
            headers = self.firmware_headers(firmware)
            if range_header:
                prefix = "bytes="
                assert range_header.startswith(prefix) and range_header.endswith("-")
                offset = int(range_header[len(prefix) : -1])
                assert 0 < offset < len(firmware)
                body = firmware[offset:]
                status = 206
                headers["Content-Range"] = (
                    f"bytes {offset}-{len(firmware) - 1}/{len(firmware)}"
                )
            if self.mock.corrupt_download:
                body = bytes([body[0] ^ 0xFF]) + body[1:]
            self.reply(status, body, headers)
        except BaseException as error:
            self.mock.failure = error
            raise

    def do_POST(self) -> None:
        try:
            if not self.authenticated():
                return
            assert self.path == "/update"
            length = int(self.headers["Content-Length"])
            body = self.rfile.read(length)
            assert b'name="MD5"' in body
            assert b'name="firmware"' in body
            if self.mock.expected_upload is not None:
                assert md5(self.mock.expected_upload).encode("ascii") in body
                assert self.mock.expected_upload in body
            self.mock.upload_count += 1
            if self.mock.accept_upload_hash:
                self.mock.current_hash = self.mock.uploaded_hash
            self.reply(200, self.mock.upload_response)
        except BaseException as error:
            self.mock.failure = error
            raise


def start_server(server: MockServer) -> threading.Thread:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return thread


def stop_server(server: MockServer, thread: threading.Thread) -> None:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)
    if server.failure:
        raise AssertionError("Mock server failed") from server.failure


def run(
    executable: Path,
    server: MockServer,
    arguments: list[str],
    *,
    timeout: float = 10,
) -> subprocess.CompletedProcess[str]:
    command = [
        str(executable),
        "--no-color",
        "--quiet",
        "--endpoint",
        f"http://localhost:{server.server_port}/update",
        "--bearer",
        server.secret,
        "--connect-timeout-ms",
        "2000",
        "--request-timeout-ms",
        "2000",
        "--initial-wait-ms",
        "1",
        "--poll-interval-ms",
        "10",
        *arguments,
    ]
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env={**os.environ, "NO_COLOR": "1"},
        timeout=timeout,
        check=False,
    )


def write_build(root: Path, raw: bytes) -> tuple[Path, bytes, str]:
    compressed = gzip.compress(raw, mtime=0)
    firmware = root / "fixture.bin"
    firmware.write_bytes(compressed)
    firmware.with_suffix(".bin.md5").write_text(
        f"{md5(raw)} *fixture.bin\n"
        f"{md5(compressed)} *fixture.bin (compressed)\n",
        encoding="utf-8",
    )
    return firmware, compressed, md5(raw)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: integration_test.py FirmwareTransferCpp.exe")
    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        raise AssertionError(f"missing uploader: {executable}")

    with tempfile.TemporaryDirectory(prefix="firmware-transfer-cpp-") as temporary:
        root = Path(temporary)
        new_raw = b"new firmware image\n" + bytes(range(256)) * 128
        old_raw = b"old firmware image\n" + bytes(reversed(range(256))) * 64
        firmware, compressed, new_hash = write_build(root, new_raw)

        # Strict local validation computes both hashes from the gzip stream.
        check_server = MockServer(current_firmware=old_raw, uploaded_hash=new_hash)
        checked = run(executable, check_server, ["--check", str(firmware)])
        assert checked.returncode == 0, checked.stdout + checked.stderr
        assert not check_server.authorizations

        malformed = root / "malformed.bin.md5"
        malformed.write_text(f"{md5(new_raw)} *fixture.bin\n", encoding="ascii")
        malformed_result = run(
            executable,
            check_server,
            ["--check", "--manifest", str(malformed), str(firmware)],
        )
        assert malformed_result.returncode == 3, (
            malformed_result.stdout + malformed_result.stderr
        )
        assert not check_server.authorizations

        wrong_raw = root / "wrong-raw.bin.md5"
        wrong_raw.write_text(
            f"{'0' * 32} *fixture.bin\n"
            f"{md5(compressed)} *fixture.bin (compressed)\n",
            encoding="ascii",
        )
        wrong_result = run(
            executable,
            check_server,
            ["--check", "--manifest", str(wrong_raw), str(firmware)],
        )
        assert wrong_result.returncode == 6, wrong_result.stdout + wrong_result.stderr

        # Upload preflight, verified backup, exact acknowledgement, and post-hash.
        backup = root / "backup.bin"
        upload_server = MockServer(
            current_firmware=old_raw,
            uploaded_hash=new_hash,
            expected_upload=compressed,
        )
        upload_thread = start_server(upload_server)
        uploaded = run(
            executable,
            upload_server,
            ["--backup", str(backup), str(firmware)],
        )
        stop_server(upload_server, upload_thread)
        assert uploaded.returncode == 0, uploaded.stdout + uploaded.stderr
        assert upload_server.upload_count == 1
        assert backup.read_bytes() == old_raw
        assert upload_server.authorizations
        assert all(
            value == f"Bearer {upload_server.secret}"
            for value in upload_server.authorizations
        )

        # Identical preflight is a successful no-op unless --force is present.
        skip_server = MockServer(current_firmware=new_raw, uploaded_hash=new_hash)
        skip_thread = start_server(skip_server)
        skipped = run(executable, skip_server, [str(firmware)])
        stop_server(skip_server, skip_thread)
        assert skipped.returncode == 0, skipped.stdout + skipped.stderr
        assert skip_server.upload_count == 0

        exact_server = MockServer(
            current_firmware=new_raw,
            uploaded_hash=new_hash,
            expected_upload=compressed,
            upload_response=b"ok!\n",
        )
        exact_thread = start_server(exact_server)
        exact = run(
            executable,
            exact_server,
            ["--force", "--no-verify", str(firmware)],
        )
        stop_server(exact_server, exact_thread)
        assert exact.returncode == 7, exact.stdout + exact.stderr
        assert 'exact expected "ok!"' in exact.stderr

        # Bounded post-reboot verification has a stable dedicated exit code.
        mismatch_server = MockServer(
            current_firmware=old_raw,
            uploaded_hash=new_hash,
            expected_upload=compressed,
            accept_upload_hash=False,
        )
        mismatch_thread = start_server(mismatch_server)
        mismatch = run(
            executable,
            mismatch_server,
            ["--reboot-timeout-ms", "350", str(firmware)],
        )
        stop_server(mismatch_server, mismatch_thread)
        assert mismatch.returncode == 8, mismatch.stdout + mismatch.stderr

        # /update/info compatibility fallback reaches legacy plain /info.
        fallback_server = MockServer(
            current_firmware=new_raw,
            uploaded_hash=new_hash,
            update_info_available=False,
        )
        fallback_thread = start_server(fallback_server)
        fallback = run(executable, fallback_server, [str(firmware)])
        stop_server(fallback_server, fallback_thread)
        assert fallback.returncode == 0, fallback.stdout + fallback.stderr
        assert fallback_server.upload_count == 0

        # Download resumes one exact byte range, verifies all hashes, then renames.
        target = root / "download.bin"
        part = Path(str(target) + ".part")
        offset = 111
        part.write_bytes(old_raw[:offset])
        download_server = MockServer(current_firmware=old_raw, uploaded_hash=new_hash)
        download_thread = start_server(download_server)
        downloaded = run(
            executable,
            download_server,
            ["--download", str(target)],
        )
        stop_server(download_server, download_thread)
        assert downloaded.returncode == 0, downloaded.stdout + downloaded.stderr
        assert target.read_bytes() == old_raw
        assert not part.exists()
        assert f"bytes={offset}-" in download_server.range_headers

        # A same-length corrupt response is never published and poisons no resume.
        corrupt_target = root / "corrupt.bin"
        corrupt_part = Path(str(corrupt_target) + ".part")
        corrupt_server = MockServer(
            current_firmware=old_raw,
            uploaded_hash=new_hash,
            corrupt_download=True,
        )
        corrupt_thread = start_server(corrupt_server)
        corrupt = run(
            executable,
            corrupt_server,
            ["--download", str(corrupt_target)],
        )
        stop_server(corrupt_server, corrupt_thread)
        assert corrupt.returncode == 6, corrupt.stdout + corrupt.stderr
        assert not corrupt_target.exists()
        assert not corrupt_part.exists()

    print(
        "C++ loopback tests passed: strict gzip/manifest, auth, preflight/skip, "
        "backup, exact upload, bounded post-verify, fallback, atomic resume, hashes."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
