#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
"""Receive the phone's PCM stream and expose it through an ALSA loopback card."""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import math
import os
import re
import secrets
import signal
import ssl
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from auxmic_pcm import AlsaOutput, WaveOutput, RATE, BLOCK_BYTES

SCHEMA_VERSION = 1
SERVICE_INSTANCE_ID = secrets.token_hex(8)
MAX_JSON_BYTES = 64 * 1024
MAX_CHUNK_BYTES = RATE * 2
BUFFER_BYTES = RATE * 2 * 2
PRIME_BYTES = RATE * 2
DEFAULT_IDLE_TIMEOUT_S = 10.0


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


class ServiceError(Exception):
    def __init__(self, status, message, *, details=None):
        super().__init__(message)
        self.status, self.message, self.details = status, message, details


class Microphone:
    """Own the current session and its one bounded PCM buffer."""

    def __init__(self, output, idle_timeout_s=DEFAULT_IDLE_TIMEOUT_S):
        self.output = output
        self.idle_timeout_s = idle_timeout_s
        self.lock = threading.RLock()
        self.session = None
        self.buffer = bytearray()
        self.primed = False
        self.prime_started = time.monotonic()
        self.last_activity = 0.0
        self.frames_written = 0
        self.dropped_frames = 0
        self.underrun_frames = 0
        self.device_ready = False
        self.last_error = ""
        self.stopping = threading.Event()

    def _clear(self):
        self.dropped_frames += len(self.buffer) // 2
        self.buffer.clear()
        self.primed = False
        self.prime_started = time.monotonic()

    def _expire(self):
        if (self.session and self.session["state"] == "OPEN"
                and time.monotonic() - self.last_activity > self.idle_timeout_s):
            self.session.update(state="EXPIRED", expiration_reason="idle_timeout")
            self._clear()

    def _authorize(self, session_id, token):
        self._expire()
        if self.session is None or self.session["session_id"] != session_id:
            raise ServiceError(404, "session not found")
        if not isinstance(token, str) or not hmac.compare_digest(
                hashlib.sha256(token.encode()).hexdigest(), self.token_hash):
            raise ServiceError(401, "invalid session token")
        return self.session

    def create(self, metadata):
        reported = metadata.get("audio_context_sample_rate")
        if reported is not None and (type(reported) is not int or reported != RATE):
            raise ServiceError(400, "the device requires 48000 Hz audio")
        with self.lock:
            self._expire()
            if (self.session and self.session["state"] == "OPEN"
                    and metadata.get("replace_existing") is not True):
                raise ServiceError(409, "another session is already active")
            self._clear()
            token = secrets.token_urlsafe(24)
            self.token_hash = hashlib.sha256(token.encode()).hexdigest()
            self.last_activity = time.monotonic()
            self.session = {
                "schema_version": SCHEMA_VERSION,
                "session_id": secrets.token_hex(8),
                "state": "OPEN",
                "created_utc": utc_now(),
                "chunks_accepted": 0,
                "gap_count": 0,
                "expected_seq": 0,
                "last_seq": None,
                "last_received_utc": None,
                "receive_rate_bps": 0.0,
                "byte_count": 0,
                "sample_count": 0,
                "sample_rate": RATE,
                "channels": 1,
            }
            return self.session["session_id"], token

    def status(self, session_id, token):
        with self.lock:
            return dict(self._authorize(session_id, token))

    def stop(self, session_id, token):
        with self.lock:
            session = self._authorize(session_id, token)
            if session["state"] == "OPEN":
                session.update(state="STOPPED", stopped_utc=utc_now())
                self._clear()
            return {"state": session["state"]}

    def accept(self, session_id, token, seq, sample_count, sample_rate, channels, payload):
        with self.lock:
            session = self._authorize(session_id, token)
            if session["state"] != "OPEN":
                raise ServiceError(409, "session is " + session["state"].lower())
            if sample_rate != RATE or channels != 1:
                raise ServiceError(400, "audio must be 48000 Hz mono s16le")
            if sample_count * 2 != len(payload):
                raise ServiceError(400, "sample count and body length do not agree")
            if seq < session["expected_seq"]:
                raise ServiceError(409, "duplicate or late sequence number",
                                   details={"expected_seq": session["expected_seq"]})
            now = time.monotonic()
            elapsed = now - self.last_activity
            session["receive_rate_bps"] = len(payload) * 8 / elapsed if elapsed > 0 else 0
            session["gap_count"] += seq - session["expected_seq"]
            session["expected_seq"] = seq + 1
            session["last_seq"] = seq
            session["chunks_accepted"] += 1
            session["byte_count"] += len(payload)
            session["sample_count"] += sample_count
            session["last_received_utc"] = utc_now()
            self.last_activity = now
            if not self.buffer and not self.primed:
                self.prime_started = now
            self.buffer.extend(payload)
            excess = max(0, len(self.buffer) - BUFFER_BYTES)
            if excess:
                del self.buffer[:excess]
                self.dropped_frames += excess // 2
            return {"accepted": True, "seq": seq, "gap_count": session["gap_count"],
                    "byte_count": session["byte_count"], "sample_count": session["sample_count"]}

    def next_block(self):
        with self.lock:
            self._expire()
            if (len(self.buffer) >= PRIME_BYTES or
                    self.buffer and time.monotonic() - self.prime_started >= 2.0):
                self.primed = True
            if not self.primed:
                return bytes(BLOCK_BYTES)
            count = min(len(self.buffer), BLOCK_BYTES)
            block = bytes(self.buffer[:count])
            del self.buffer[:count]
            if count < BLOCK_BYTES:
                # Refill after an underrun instead of emitting isolated fragments.
                self.primed = False
                self.prime_started = time.monotonic()
                if self.session and self.session["state"] == "OPEN":
                    self.underrun_frames += (BLOCK_BYTES - count) // 2
            return block.ljust(BLOCK_BYTES, b"\0")

    def registry(self):
        with self.lock:
            self._expire()
            session = self.session or {}
            return {
                "state": "streaming" if session.get("state") == "OPEN" else "idle",
                "session_id": session.get("session_id"),
                "format": "s16le", "rate": RATE, "channels": 1,
                "device_ready": self.device_ready,
                "buffered_frames": len(self.buffer) // 2,
                "dropped_frames": self.dropped_frames,
                "underrun_frames": self.underrun_frames,
                "frames_written": self.frames_written,
                "last_error": self.last_error,
            }

def load_or_create_pairing_token(path: Path) -> str:
    """The credential that decides who may become the source."""
    try:
        token = path.read_text(encoding="utf-8").strip()
        if token:
            return token
    except FileNotFoundError:
        pass
    token = secrets.token_hex(4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(token + "\n", encoding="utf-8")
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass
    return token


class RequestHandler(BaseHTTPRequestHandler):
    server_version = "Auxmic/1.0"
    # Reuse the connection for the phone's 250 ms chunks.
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True

    @property
    def microphone(self):
        return self.server.microphone

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, fmt, *args):
        if len(args) > 1 and str(args[1]).isdigit() and int(args[1]) >= 400:
            sys.stderr.write("%s - %s\n" % (self.log_date_time_string(), fmt % args))

    def _send(self, status: int, payload: Any, *, content_type: str = "application/json; charset=utf-8") -> None:
        if content_type.startswith("application/json"):
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        else:
            body = payload if isinstance(payload, bytes) else str(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _error(self, error: ServiceError) -> None:
        payload: dict[str, Any] = {"error": error.message}
        if error.details is not None:
            payload["details"] = error.details
        self._send(error.status, payload)

    def _body(self, limit: int, allow_empty: bool = False) -> bytes:
        value = self.headers.get("Content-Length")
        try:
            length = int(value) if value is not None else -1
        except ValueError:
            length = -1
        if length < 0:
            if allow_empty and value is None:
                return b""
            self.close_connection = True
            raise ServiceError(411, "Content-Length is required")
        if length > limit:
            self.close_connection = True
            raise ServiceError(413, "request body is too large")
        body = self.rfile.read(length)
        if len(body) != length:
            raise ServiceError(400, "request body ended early")
        return body

    def _json(self, limit: int = MAX_JSON_BYTES, allow_empty: bool = False) -> dict[str, Any]:
        body = self._body(limit, allow_empty)
        if not body.strip():
            if allow_empty:
                return {}
            raise ServiceError(400, "request body must be a JSON object")
        try:
            value = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ServiceError(400, "request body must be valid UTF-8 JSON") from exc
        if not isinstance(value, dict):
            raise ServiceError(400, "request body must be a JSON object")
        return value

    def _token(self) -> str | None:
        auth = self.headers.get("Authorization", "")
        if auth.lower().startswith("bearer "):
            return auth[7:].strip()
        return None

    def _phone_revision(self) -> str:
        """Return the current page fingerprint so a phone can reload safely."""
        try:
            page = self.server.phone_html.read_bytes()  # type: ignore[attr-defined]
        except OSError:
            return "unavailable"
        return hashlib.sha256(page).hexdigest()[:16]

    def _check_pairing(self, given: str | None) -> None:
        """Only a paired phone may become the source.

        The code is hexadecimal, so a phone keyboard that capitalises it
        still means the same code.
        """
        expected = self.server.pairing_token  # type: ignore[attr-defined]
        if not isinstance(given, str) or not given or not hmac.compare_digest(given.strip().lower(), expected.lower()):
            raise ServiceError(403, "the pairing code is wrong")

    def do_GET(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path.rstrip("/") or "/"
            if path in ("/", "/auxmic"):
                page_path = self.server.phone_html  # type: ignore[attr-defined]
                if not page_path.is_file():
                    raise ServiceError(404, "phone page not found")
                page = page_path.read_bytes()
                self._send(200, page, content_type="text/html; charset=utf-8")
                return
            if path == "/v1/status":
                self._send(200, {
                    "service": "auxmic",
                    "schema_version": SCHEMA_VERSION,
                    "instance_id": SERVICE_INSTANCE_ID,
                    "phone_revision": self._phone_revision(),
                    "time_utc": utc_now(),
                })
                return
            if path == "/healthz":
                self._send(200, {
                    "status": "ok",
                    "service": "auxmic",
                    "schema_version": SCHEMA_VERSION,
                    "instance_id": SERVICE_INSTANCE_ID,
                    "phone_revision": self._phone_revision(),
                })
                return
            if path == "/v1/auxmic/source":
                self._send(200, self.microphone.registry())
                return
            match = re.fullmatch(r"/v1/mic/([0-9a-f]{12,32})/status", path)
            if match:
                self._send(200, self.microphone.status(match.group(1), self._token()))
                return
            raise ServiceError(404, "not found")
        except ServiceError as error:
            self.close_connection = True
            self._error(error)
        except (ConnectionError, TimeoutError):
            self.close_connection = True
        except Exception:
            self.close_connection = True
            self.server.handle_error(self.request, self.client_address)
            self._error(ServiceError(500, "internal server error"))

    def do_POST(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path.rstrip("/")
            if path == "/v1/mic/session":
                body = self._json()
                self._check_pairing(body.get("pairing") or self.headers.get("X-Auxmic-Pair"))
                session_id, token = self.microphone.create(body)
                self._send(201, {
                    "session_id": session_id,
                    "token": token,
                    "source": "phone_internal",
                    "transport": "pcm_chunk",
                    "status_url": f"/v1/mic/{session_id}/status",
                    "chunk_url": f"/v1/mic/{session_id}/chunk",
                    "stop_url": f"/v1/mic/{session_id}/stop",
                })
                return
            match = re.fullmatch(r"/v1/mic/([0-9a-f]{12,32})/(chunk|stop)", path)
            if match:
                session_id, operation = match.group(1), match.group(2)
                if operation == "stop":
                    # A beacon cannot set request headers, so the token may
                    # arrive in the body instead.  Either way the caller has
                    # to prove it owns the session.
                    body = self._json(allow_empty=True)
                    result = self.microphone.stop(
                        session_id, self._token() or body.get("token"))
                    self._send(200, result)
                    return
                self._handle_chunk(session_id)
                return
            raise ServiceError(404, "not found")
        except ServiceError as error:
            self.close_connection = True
            self._error(error)
        except (ConnectionError, TimeoutError):
            self.close_connection = True
        except Exception:
            self.close_connection = True
            self.server.handle_error(self.request, self.client_address)
            self._error(ServiceError(500, "internal server error"))

    def _handle_chunk(self, session_id) -> None:
        def header_int(name: str, *, minimum: int, maximum: int) -> int:
            raw = self.headers.get(name)
            try:
                value = int(raw) if raw is not None else -1
            except ValueError:
                value = -1
            if not minimum <= value <= maximum:
                raise ServiceError(400, f"{name} is out of range")
            return value

        seq = header_int("X-Seq", minimum=0, maximum=2**53 - 1)
        sample_count = header_int("X-Sample-Count", minimum=1, maximum=2**24)
        sample_rate = header_int("X-Sample-Rate", minimum=44100, maximum=48000)
        channels = header_int("X-Channels", minimum=1, maximum=1)
        payload = self._body(MAX_CHUNK_BYTES)
        result = self.microphone.accept(
            session_id, self._token(), seq, sample_count, sample_rate, channels, payload)
        self._send(200, result)



class AuxmicServer(ThreadingHTTPServer):
    def __init__(self, address, microphone, pairing_token, phone_html):
        super().__init__(address, RequestHandler)
        self.microphone = microphone
        self.pairing_token = pairing_token
        self.phone_html = Path(phone_html)
        self.tls_context = None

    def get_request(self):
        connection, address = super().get_request()
        # Bound TLS negotiation too, before RequestHandler.setup can run.
        connection.settimeout(5)
        try:
            if self.tls_context is not None:
                connection = self.tls_context.wrap_socket(connection, server_side=True)
            return connection, address
        except Exception:
            connection.close()
            raise

    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):
            return
        super().handle_error(request, client_address)


def serve(server):
    microphone = server.microphone
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    try:
        microphone.output.open()
        microphone.device_ready = True
        thread.start()
        while not microphone.stopping.is_set():
            data = microphone.next_block()
            microphone.output.write(data)
            with microphone.lock:
                microphone.frames_written += len(data) // 2
    except Exception as error:
        microphone.last_error = str(error)
        raise
    finally:
        microphone.device_ready = False
        if thread.is_alive():
            server.shutdown()
            thread.join()
        server.server_close()
        microphone.output.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9443)
    parser.add_argument("--config-dir", type=Path,
                        default=Path("/home/lava/printer_data/config/extended/auxmic"))
    parser.add_argument("--phone-html", default="/usr/local/share/auxmic/phone.html")
    parser.add_argument("--device", default="hw:Loopback,0,0")
    parser.add_argument("--output-wav", type=Path, help="local debug output instead of ALSA")
    parser.add_argument("--idle-timeout-s", type=float, default=DEFAULT_IDLE_TIMEOUT_S)
    parser.add_argument("--insecure-http", action="store_true", help="localhost debugging only")
    args = parser.parse_args(argv)
    if not math.isfinite(args.idle_timeout_s) or args.idle_timeout_s <= 0:
        parser.error("--idle-timeout-s must be positive")
    if args.insecure_http and args.host not in ("127.0.0.1", "::1", "localhost"):
        parser.error("--insecure-http is limited to localhost")
    output = WaveOutput(args.output_wav) if args.output_wav else AlsaOutput(args.device)
    microphone = Microphone(output, args.idle_timeout_s)
    token = load_or_create_pairing_token(args.config_dir / "pairing.token")
    server = AuxmicServer((args.host, args.port), microphone, token, args.phone_html)
    try:
        if not args.insecure_http:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(args.config_dir / "server.crt", args.config_dir / "server.key")
            server.tls_context = context
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, lambda *_: microphone.stopping.set())
        serve(server)
    except Exception as error:
        print(f"auxmic: {error}", file=sys.stderr)
        server.server_close()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
