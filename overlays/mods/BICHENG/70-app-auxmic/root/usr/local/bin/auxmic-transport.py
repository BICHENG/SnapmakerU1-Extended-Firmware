#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
"""Phone microphone transport for the auxmic mod.

The transport is the phone facing half of auxmic.  It speaks HTTPS to the
phone page, keeps the session, and hands the audio over to the device half
(the pacer) as it arrives.  It knows nothing about sound cards.

The link to the device half is the source: the transport connects while a
source is live and disconnects when it ends, so the device turns to silence
by itself.  Audio that the device half cannot take is dropped and counted
rather than held back, because a phone that waits is a phone that drifts
out of sync with the machine.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import math
import os
import re
import secrets
import socket
import ssl
import sys
import threading
import time
import uuid
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

SCHEMA_VERSION = 1
SERVICE_INSTANCE_ID = secrets.token_hex(8)
DEVICE_RATE = 48000
MAX_JSON_BYTES = 64 * 1024
MAX_CHUNK_BYTES = 4 * 1024 * 1024
DEFAULT_IDLE_TIMEOUT_S = 600.0
SESSION_ID_RE = re.compile(r"^[0-9a-f]{12,32}$")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def as_int(value: Any, name: str, *, minimum: int | None = None, maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    number = int(value)
    if minimum is not None and number < minimum:
        raise ValueError(f"{name} is below {minimum}")
    if maximum is not None and number > maximum:
        raise ValueError(f"{name} is above {maximum}")
    return number


class ServiceError(Exception):
    def __init__(self, status: int, message: str, *, details: Any = None):
        super().__init__(message)
        self.status = status
        self.message = message
        self.details = details


class SourceLink:
    """Carries the audio of the live source to the device half.

    The connection is the source.  While it is up the audio is live; when it
    goes away the device half turns to silence on its own.  A bounded queue
    absorbs the jitter of the delivery; audio that cannot be handed over is
    dropped and counted, because a phone that waits drifts out of sync with
    the machine.
    """

    def __init__(self, host: str, port: int, queue_bytes: int) -> None:
        self.host = host
        self.port = port
        self.queue_bytes = queue_bytes
        self.lock = threading.RLock()
        self.wake = threading.Condition(self.lock)
        self.queue = bytearray()
        self.sock: socket.socket | None = None
        self.running = False
        self.finished = False
        self.thread: threading.Thread | None = None
        self.thread_started = False
        self.retry_delay_s = 1.0
        self.epoch = 0
        self.state = "idle"
        self.session_id: str | None = None
        self.source_id = secrets.token_hex(8)
        self.started_utc: str | None = None
        self.frames = 0
        self.dropped_bytes = 0
        self.last_error = ""
        self.connected = False

    def start(self, session_id: str) -> None:
        """Begin a new source.  The previous one is replaced."""
        with self.lock:
            self.epoch += 1
            self.state = "streaming"
            self.session_id = session_id
            self.started_utc = utc_now()
            self.frames = 0
            self.queue = bytearray()
            self.running = True
            if not self.thread_started:
                self.thread_started = True
                self.thread = threading.Thread(target=self._writer, daemon=True)
                self.thread.start()
            self.wake.notify_all()

    def stop(self) -> None:
        """End the source.  The device half sees the disconnect and goes quiet."""
        with self.lock:
            self.running = False
            self.state = "idle"
            self.session_id = None
            self.queue = bytearray()
            sock = self.sock
            self.sock = None
            self.connected = False
            self.wake.notify_all()
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def close(self) -> None:
        """Shut the writer down for good."""
        with self.lock:
            self.finished = True
            self.running = False
            self.wake.notify_all()
        self.stop()

    def push(self, payload: bytes) -> None:
        """Hand audio over.  Never blocks the request that delivered it."""
        with self.lock:
            if self.state != "streaming":
                return
            self.queue.extend(payload)
            if len(self.queue) > self.queue_bytes:
                excess = len(self.queue) - self.queue_bytes
                del self.queue[:excess]
                self.dropped_bytes += excess
            self.wake.notify_all()

    def _connect(self) -> socket.socket | None:
        try:
            sock = socket.create_connection((self.host, self.port), timeout=2.0)
            sock.settimeout(2.0)
            return sock
        except OSError as exc:
            with self.lock:
                self.last_error = str(exc)
            return None

    def _writer(self) -> None:
        """Move the queued audio to the device half.

        The thread lives as long as the service does.  A source that ends
        empties the queue and closes the socket; the writer stays awake for
        the next source instead of exiting, because a writer that exits
        between one source and the next leaves the machine silent forever.
        """
        while True:
            with self.lock:
                if self.finished:
                    return
                if not self.running and not self.queue:
                    self.wake.wait(timeout=0.2)
                    continue
                payload = bytes(self.queue[:65536])
                del self.queue[:len(payload)]
                sock = self.sock
            if sock is None:
                sock = self._connect()
                if sock is None:
                    # The device half is not there.  Drop what we hold rather
                    # than hand over audio from minutes ago.
                    with self.lock:
                        self.dropped_bytes += len(payload) + len(self.queue)
                        self.queue = bytearray()
                    time.sleep(self.retry_delay_s)
                    continue
                with self.lock:
                    if self.finished or not self.running:
                        self.dropped_bytes += len(payload)
                        try:
                            sock.close()
                        except OSError:
                            pass
                        continue
                    self.sock = sock
                    self.connected = True
            try:
                sock.sendall(payload)
            except OSError as exc:
                with self.lock:
                    self.last_error = str(exc)
                    self.dropped_bytes += len(payload)
                    if self.sock is sock:
                        self.sock = None
                        self.connected = False
                try:
                    sock.close()
                except OSError:
                    pass
                continue
            with self.lock:
                self.frames += len(payload) // 2

    def registry(self) -> dict[str, Any]:
        """The source table: who is feeding the device right now."""
        with self.lock:
            return {
                "source_id": self.source_id,
                "epoch": self.epoch,
                "state": self.state,
                "session_id": self.session_id,
                "started_utc": self.started_utc,
                "format": "s16le",
                "rate": DEVICE_RATE,
                "channels": 1,
                "frames": self.frames,
                "dropped_bytes": self.dropped_bytes,
                "link_connected": self.connected,
                "last_error": self.last_error,
            }


class Session:
    def __init__(self, session_id: str, created_utc: str, token_hash: str,
                 idle_timeout_s: float):
        self.token_hash = token_hash
        self.idle_timeout_s = idle_timeout_s
        self.lock = threading.RLock()
        self.status: dict[str, Any] = {
            "schema_version": SCHEMA_VERSION,
            "session_id": session_id,
            "state": "OPEN",
            "created_utc": created_utc,
            "last_activity_utc": created_utc,
            "chunks_accepted": 0,
            "gap_count": 0,
            "expected_seq": 0,
            "last_seq": None,
            "last_received_utc": None,
            "receive_rate_bps": 0.0,
            "byte_count": 0,
            "sample_count": 0,
            "sample_rate": None,
            "channels": None,
        }
        self._last_received_monotonic_ns: int | None = None

    @property
    def session_id(self) -> str:
        return self.status["session_id"]

    def check_token(self, token: str | None) -> None:
        if not token or not hmac.compare_digest(hashlib.sha256(token.encode("utf-8")).hexdigest(), self.token_hash):
            raise ServiceError(401, "invalid session token")

    def expire_if_idle(self, now: float | None = None) -> bool:
        """Finalize a disconnected OPEN session."""
        with self.lock:
            if self.status.get("state") != "OPEN":
                return False
            last = datetime.fromisoformat(
                self.status["last_activity_utc"].replace("Z", "+00:00")).timestamp()
            current = time.time() if now is None else now
            if current - last <= self.idle_timeout_s:
                return False
            self.status["state"] = "EXPIRED"
            self.status["expired_utc"] = utc_now()
            self.status["expiration_reason"] = "idle_timeout"
            return True

    def append_chunk(self, seq: int, sample_count: int, sample_rate: int,
                     channels: int, payload: bytes) -> dict[str, Any]:
        with self.lock:
            if self.status["state"] != "OPEN":
                raise ServiceError(409, f"session is {self.status['state'].lower()}")
            expected_bytes = sample_count * channels * 2
            if expected_bytes != len(payload):
                raise ServiceError(400, "sample_count, channels and body length do not agree", details={
                    "expected_bytes": expected_bytes,
                    "actual_bytes": len(payload),
                })
            if seq < self.status["expected_seq"]:
                raise ServiceError(409, "duplicate or late sequence number", details={"expected_seq": self.status["expected_seq"]})
            if sample_rate != DEVICE_RATE:
                # The device half writes into a sound card that runs at one
                # rate.  Audio of another rate would be played at the wrong
                # speed, so it is refused here rather than passed on.
                raise ServiceError(400, f"the device runs at {DEVICE_RATE} Hz; this source is {sample_rate} Hz")
            if channels != 1:
                raise ServiceError(400, "channels must be mono (1)")
            previous_rate = self.status["sample_rate"]
            previous_channels = self.status["channels"]
            if previous_rate is not None and (sample_rate != previous_rate or channels != previous_channels):
                raise ServiceError(409, "sample rate and channels must stay constant within a session")

            gap = seq - self.status["expected_seq"]
            self.status["chunks_accepted"] += 1
            self.status["gap_count"] += gap
            self.status["expected_seq"] = seq + 1
            self.status["last_seq"] = seq
            self.status["byte_count"] += len(payload)
            self.status["sample_count"] += sample_count
            self.status["sample_rate"] = sample_rate
            self.status["channels"] = channels
            received_ns = time.monotonic_ns()
            previous_received_ns = self._last_received_monotonic_ns
            self._last_received_monotonic_ns = received_ns
            self.status["last_received_utc"] = utc_now()
            self.status["last_activity_utc"] = self.status["last_received_utc"]
            if previous_received_ns is not None and received_ns > previous_received_ns:
                self.status["receive_rate_bps"] = len(payload) * 8 / ((received_ns - previous_received_ns) / 1_000_000_000)
            else:
                self.status["receive_rate_bps"] = 0.0
            return {
                "accepted": True,
                "seq": seq,
                "gap_count": self.status["gap_count"],
                "byte_count": self.status["byte_count"],
                "sample_count": self.status["sample_count"],
            }

    def stop(self) -> dict[str, Any]:
        with self.lock:
            if self.status["state"] == "OPEN":
                self.status["state"] = "STOPPED"
                self.status["stopped_utc"] = utc_now()
            return {"state": self.status["state"]}

    def public_status(self) -> dict[str, Any]:
        with self.lock:
            return dict(self.status)


class SessionStore:
    def __init__(self, idle_timeout_s: float = DEFAULT_IDLE_TIMEOUT_S,
                 link: SourceLink | None = None):
        self.idle_timeout_s = idle_timeout_s
        self.link = link
        self.lock = threading.RLock()
        self.create_lock = threading.Lock()
        self.sessions: dict[str, Session] = {}

    def create(self, metadata: dict[str, Any]) -> tuple[Session, str]:
        with self.create_lock:
            return self._create(metadata)

    def _create(self, metadata: dict[str, Any]) -> tuple[Session, str]:
        if not isinstance(metadata, dict):
            raise ServiceError(400, "request body must be a JSON object")
        with self.lock:
            reported = metadata.get("audio_context_sample_rate")
            if isinstance(reported, int) and reported != DEVICE_RATE:
                # The browser told us up front what it can do.  Say so now,
                # instead of letting every chunk fail later.
                raise ServiceError(400, f"the device runs at {DEVICE_RATE} Hz; this browser offers {reported} Hz")
            active = self.active()
            replace_existing = metadata.get("replace_existing") is True
            if active is not None:
                if not replace_existing:
                    raise ServiceError(409, "another session is already active", details={"session_id": active.session_id})
                active.stop()
                if self.link is not None:
                    self.link.stop()
            session_id = uuid.uuid4().hex[:16]
            token = secrets.token_urlsafe(24)
            now = utc_now()
            token_hash = hashlib.sha256(token.encode("utf-8")).hexdigest()
            session = Session(session_id, now, token_hash, self.idle_timeout_s)
            self.sessions[session_id] = session
            if self.link is not None:
                self.link.start(session_id)
            return session, token

    def active(self) -> Session | None:
        """Return the one live OPEN session."""
        for session in list(self.sessions.values()):
            if session.expire_if_idle():
                if self.link is not None and self.link.session_id == session.session_id:
                    self.link.stop()
                continue
            if session.status.get("state") == "OPEN":
                return session
        return None

    def get(self, session_id: str) -> Session:
        if not SESSION_ID_RE.fullmatch(session_id):
            raise ServiceError(404, "session not found")
        with self.lock:
            session = self.sessions.get(session_id)
            if session is None:
                raise ServiceError(404, "session not found")
            session.expire_if_idle()
            return session


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
    server_version = "AuxmicTransport/0.1"
    # Every response carries a Content-Length, so the connection can be kept
    # open.  The phone sends a block every 250 ms; opening a socket for each
    # one would be waste.
    protocol_version = "HTTP/1.1"
    # The answer goes out in two writes, the headers and then the body.  With
    # Nagle on, the second write waits for the phone to acknowledge the first
    # one, and the phone holds its acknowledgement back for tens of
    # milliseconds.  That wait is added to every block the phone sends, so
    # the socket carries the answer without holding it back.
    disable_nagle_algorithm = True

    @property
    def store(self) -> SessionStore:
        return self.server.store  # type: ignore[attr-defined]

    @property
    def link(self) -> SourceLink:
        return self.server.link  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
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
            if allow_empty:
                return b""
            raise ServiceError(411, "Content-Length is required")
        if length > limit:
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

    def _session(self, session_id: str) -> Session:
        session = self.store.get(session_id)
        session.check_token(self._token())
        return session

    def _check_pairing(self, given: str | None) -> None:
        """Only a paired phone may become the source.

        The code is hexadecimal, so a phone keyboard that capitalises it
        still means the same code.
        """
        expected = self.server.pairing_token  # type: ignore[attr-defined]
        if not given or not hmac.compare_digest(given.strip().lower(), expected.lower()):
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
                self._send(200, self.link.registry())
                return
            match = re.fullmatch(r"/v1/mic/([0-9a-f]{12,32})/status", path)
            if match:
                session = self._session(match.group(1))
                self._send(200, session.public_status())
                return
            raise ServiceError(404, "not found")
        except ServiceError as error:
            self._error(error)
        except Exception as exc:  # keep the service alive on one bad request
            self._error(ServiceError(500, "internal server error", details=str(exc)))

    def do_POST(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path.rstrip("/")
            if path == "/v1/mic/session":
                body = self._json()
                self._check_pairing(body.get("pairing") or self.headers.get("X-Auxmic-Pair"))
                session, token = self.store.create(body)
                self._send(201, {
                    "session_id": session.session_id,
                    "token": token,
                    "source": "phone_internal",
                    "transport": "pcm_chunk",
                    "status_url": f"/v1/mic/{session.session_id}/status",
                    "chunk_url": f"/v1/mic/{session.session_id}/chunk",
                    "stop_url": f"/v1/mic/{session.session_id}/stop",
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
                    session = self.store.get(session_id)
                    session.check_token(self._token() or body.get("token"))
                    result = session.stop()
                    if self.link is not None and self.link.session_id == session.session_id:
                        self.link.stop()
                    self._send(200, result)
                    return
                session = self._session(session_id)
                self._handle_chunk(session)
                return
            raise ServiceError(404, "not found")
        except ServiceError as error:
            self._error(error)
        except Exception as exc:
            self._error(ServiceError(500, "internal server error", details=str(exc)))

    def _handle_chunk(self, session: Session) -> None:
        def header_int(name: str, *, minimum: int, maximum: int) -> int:
            raw = self.headers.get(name)
            try:
                value = int(raw) if raw is not None else -1
            except ValueError:
                value = -1
            try:
                return as_int(value, name, minimum=minimum, maximum=maximum)
            except ValueError as exc:
                raise ServiceError(400, str(exc)) from exc

        seq = header_int("X-Seq", minimum=0, maximum=2**53 - 1)
        sample_count = header_int("X-Sample-Count", minimum=1, maximum=2**24)
        sample_rate = header_int("X-Sample-Rate", minimum=44100, maximum=48000)
        channels = header_int("X-Channels", minimum=1, maximum=1)
        payload = self._body(MAX_CHUNK_BYTES)
        result = session.append_chunk(seq, sample_count, sample_rate, channels, payload)
        self.link.push(payload)
        self._send(200, result)


class AuxmicServer(ThreadingHTTPServer):
    """The transport's server.

    A phone that walks away closes the connection without saying goodbye.
    That is normal and it must not fill the log with a traceback.
    """

    def handle_error(self, request, client_address) -> None:
        error = sys.exc_info()[1]
        if isinstance(error, (ConnectionResetError, BrokenPipeError,
                              ConnectionAbortedError, TimeoutError)):
            return
        super().handle_error(request, client_address)


def make_server(host: str, port: int, link: SourceLink,
                pairing_token: str, certfile: str | None = None,
                keyfile: str | None = None, phone_html: str | None = None,
                idle_timeout_s: float = DEFAULT_IDLE_TIMEOUT_S) -> ThreadingHTTPServer:
    if not math.isfinite(idle_timeout_s) or idle_timeout_s <= 0:
        raise ValueError("idle_timeout_s must be positive")
    server = AuxmicServer((host, port), RequestHandler)
    server.store = SessionStore(idle_timeout_s, link)  # type: ignore[attr-defined]
    server.link = link  # type: ignore[attr-defined]
    server.pairing_token = pairing_token  # type: ignore[attr-defined]
    server.phone_html = Path(phone_html) if phone_html else Path("/usr/local/share/auxmic/phone.html")  # type: ignore[attr-defined]
    if bool(certfile) != bool(keyfile):
        server.server_close()
        raise ValueError("certfile and keyfile must be supplied together")
    if certfile and keyfile:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile, keyfile)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="bind address; use 0.0.0.0 on the printer LAN")
    parser.add_argument("--port", type=int, default=9443)
    parser.add_argument("--config-dir", type=Path,
                        default=Path("/home/lava/printer_data/config/extended/auxmic"))
    parser.add_argument("--certfile", type=Path, help="TLS certificate; defaults to the config dir")
    parser.add_argument("--keyfile", type=Path, help="TLS key; defaults to the config dir")
    parser.add_argument("--phone-html", type=Path, help="phone page to serve")
    parser.add_argument("--idle-timeout-s", type=float, default=DEFAULT_IDLE_TIMEOUT_S,
                        help="expire disconnected sessions after this many seconds")
    parser.add_argument("--link-host", default="127.0.0.1", help="where the device half listens")
    parser.add_argument("--link-port", type=int, default=9444)
    parser.add_argument("--link-queue-ms", type=int, default=2000,
                        help="audio held for the device half before the oldest is dropped")
    parser.add_argument("--insecure-http", action="store_true",
                        help="development only; do not use this on the printer LAN")
    args = parser.parse_args(argv)

    config_dir = args.config_dir
    certfile = args.certfile or (config_dir / "server.crt")
    keyfile = args.keyfile or (config_dir / "server.key")
    pairing_token = load_or_create_pairing_token(config_dir / "pairing.token")

    link = SourceLink(args.link_host, args.link_port,
                      int(DEVICE_RATE * args.link_queue_ms / 1000) * 2)
    server = make_server(args.host, args.port, link, pairing_token,
                          None if args.insecure_http else str(certfile),
                          None if args.insecure_http else str(keyfile),
                          str(args.phone_html) if args.phone_html else None,
                          args.idle_timeout_s)
    scheme = "http" if args.insecure_http else "https"
    print("auxmic transport listening at %s://%s:%d/" % (scheme, args.host, args.port), flush=True)
    print("device half: %s:%d" % (args.link_host, args.link_port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopping", flush=True)
    finally:
        link.close()
        server.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
