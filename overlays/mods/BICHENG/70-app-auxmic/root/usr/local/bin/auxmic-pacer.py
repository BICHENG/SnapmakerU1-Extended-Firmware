#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
"""Keep one capture source fed into a sound device, for the auxmic mod.

The pacer owns the output device.  It opens the device once and keeps it
open for the life of the process, so a source that appears and disappears
only changes what the device carries: sound while a source is live, silence
otherwise.  A consumer never sees a device error, an xrun, or a card that
was reopened under it.

The source is a peer that connects to this process and streams raw PCM.
The connection is the source: while it is up the data is live, when it goes
away the output turns to silence.

Two sinks are available.  ``alsa`` drives a real ALSA playback device and
is what runs on the printer.  ``file`` writes the same stream to a WAV file
at the same rate and is what the local test rig uses.

The link is mono.  The ALSA sink gives the device the channel count it asks
for and copies the mono samples into every channel.  The loopback card fixes
its channel count when the first side opens it, and the only capture tool on
the printer opens it in stereo, so the device side is stereo by default.
"""
from __future__ import annotations

import argparse
import array
import ctypes
import json
import os
import signal
import socket
import struct
import sys
import threading
import time

RATE = 48000
BITS = 16
LINK_CHANNELS = 1  # the link carries mono s16le
LINK_BYTES_PER_FRAME = LINK_CHANNELS * BITS // 8
BLOCK_FRAMES = 480  # 10 ms
BLOCK_BYTES = BLOCK_FRAMES * LINK_BYTES_PER_FRAME
PRIME_TIMEOUT_S = 2.0  # give up waiting for a full block after this long


def expand_channels(data: bytes, channels: int) -> bytes:
    """Copy mono s16le samples into every channel of an interleaved buffer."""
    if channels <= 1:
        return data
    samples = array.array("h")
    samples.frombytes(data[:len(data) - len(data) % 2])
    if sys.byteorder != "little":
        samples.byteswap()
    out = array.array("h", bytes(len(samples) * channels * 2))
    for channel in range(channels):
        out[channel::channels] = samples
    if sys.byteorder != "little":
        out.byteswap()
    return out.tobytes()


class Sink:
    """Consumes PCM at the device rate."""

    name = "sink"
    channels = LINK_CHANNELS

    def open(self) -> None:
        raise NotImplementedError

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def recover(self) -> bool:
        """Try to bring the device back after an error."""
        return False

    def close(self) -> None:
        pass


SND_PCM_STREAM_PLAYBACK = 0
SND_PCM_FORMAT_S16_LE = 2
SND_PCM_ACCESS_RW_INTERLEAVED = 3
SND_PCM_ERR_XRUN = -32


class AlsaSink(Sink):
    """Plays PCM into an ALSA device, for example hw:Loopback,0.

    The link is mono but the device gets the channel count it asks for: the
    mono samples are copied into every channel.  That matters on the loopback
    card, where the channel count is fixed by whichever side opens the card
    first, and the only capture tool on the printer opens it in stereo.
    """

    name = "alsa"

    def __init__(self, device: str, latency_us: int = 100000,
                 channels: int = 2) -> None:
        self.device = device
        self.latency_us = latency_us
        self.channels = max(1, int(channels))
        self.lib = None
        self.pcm = ctypes.c_void_p()
        self.frames = 0

    def _load(self):
        lib = ctypes.CDLL("libasound.so.2")
        lib.snd_pcm_open.argtypes = [ctypes.POINTER(ctypes.c_void_p),
                                     ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        lib.snd_pcm_open.restype = ctypes.c_int
        lib.snd_pcm_set_params.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                           ctypes.c_int, ctypes.c_uint,
                                           ctypes.c_uint, ctypes.c_int,
                                           ctypes.c_uint]
        lib.snd_pcm_set_params.restype = ctypes.c_int
        lib.snd_pcm_writei.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_ulong]
        lib.snd_pcm_writei.restype = ctypes.c_long
        lib.snd_pcm_recover.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                        ctypes.c_int]
        lib.snd_pcm_recover.restype = ctypes.c_int
        lib.snd_pcm_close.argtypes = [ctypes.c_void_p]
        lib.snd_pcm_close.restype = ctypes.c_int
        lib.snd_strerror.argtypes = [ctypes.c_int]
        lib.snd_strerror.restype = ctypes.c_char_p
        return lib

    def _strerror(self, code: int) -> str:
        try:
            return self.lib.snd_strerror(code).decode("utf-8", "replace")
        except Exception:
            return "error %d" % code

    def open(self) -> None:
        self.lib = self._load()
        rc = self.lib.snd_pcm_open(ctypes.byref(self.pcm),
                                    self.device.encode("utf-8"),
                                    SND_PCM_STREAM_PLAYBACK, 0)
        if rc < 0:
            raise RuntimeError("snd_pcm_open(%s): %s"
                               % (self.device, self._strerror(rc)))
        rc = self.lib.snd_pcm_set_params(self.pcm, SND_PCM_FORMAT_S16_LE,
                                         SND_PCM_ACCESS_RW_INTERLEAVED,
                                         self.channels, RATE, 1, self.latency_us)
        if rc < 0:
            self.close()
            raise RuntimeError("snd_pcm_set_params: %s" % self._strerror(rc))

    def _expand(self, data: bytes) -> bytes:
        """Copy the mono link samples into every channel of the device."""
        return expand_channels(data, self.channels)

    def write(self, data: bytes) -> None:
        frames = len(data) // LINK_BYTES_PER_FRAME
        if frames <= 0:
            return
        payload = self._expand(data)
        buf = ctypes.create_string_buffer(payload, len(payload))
        written = self.lib.snd_pcm_writei(self.pcm, buf, frames)
        if written < 0:
            raise RuntimeError("snd_pcm_writei: %s" % self._strerror(int(written)))
        self.frames += written

    def recover(self) -> bool:
        return self.lib.snd_pcm_recover(self.pcm, SND_PCM_ERR_XRUN, 1) == 0

    def close(self) -> None:
        if self.lib is not None and self.pcm:
            self.lib.snd_pcm_close(self.pcm)
            self.pcm = ctypes.c_void_p()


class FileSink(Sink):
    """Writes the stream to a WAV file at the same rate."""

    name = "file"

    def __init__(self, path: str) -> None:
        self.path = path
        self.handle = None
        self.frames = 0
        self.start = 0.0

    def _header(self, data_bytes: int) -> bytes:
        byte_rate = RATE * LINK_CHANNELS * 2
        return (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVEfmt "
                + struct.pack("<IHHIIHH", 16, 1, LINK_CHANNELS, RATE, byte_rate,
                              LINK_CHANNELS * 2, BITS)
                + b"data" + struct.pack("<I", data_bytes))

    def open(self) -> None:
        self.handle = open(self.path, "wb")
        self.handle.write(self._header(0))
        self.start = time.monotonic()

    def write(self, data: bytes) -> None:
        self.handle.write(data)
        self.frames += len(data) // LINK_BYTES_PER_FRAME
        target = self.start + self.frames / float(RATE)
        delay = target - time.monotonic()
        if delay > 0:
            time.sleep(delay)

    def close(self) -> None:
        if self.handle is None:
            return
        data_bytes = self.frames * LINK_BYTES_PER_FRAME
        self.handle.flush()
        self.handle.seek(0)
        self.handle.write(self._header(data_bytes))
        self.handle.close()
        self.handle = None


class Pacer:
    """Feeds the sink from whichever source is connected."""

    def __init__(self, sink: Sink, ring_ms: int = 750, port: int = 0,
                 status_path: str = "", listen_host: str = "127.0.0.1",
                 prime_ms: int = 500) -> None:
        self.sink = sink
        # The source delivers its audio in blocks, so what arrives is bursty
        # while what the device consumes is smooth.  The buffer has to hold
        # one block interval plus a margin, otherwise the seams between the
        # blocks show up as gaps in the output.
        self.ring_bytes = max(BLOCK_BYTES, int(RATE * ring_ms / 1000)
                               * LINK_BYTES_PER_FRAME)
        # A buffer that is only ever filled as fast as it is emptied keeps no
        # backlog at all, and then every bit of jitter in the delivery turns
        # into a gap in the output.  So the consumer waits until one block
        # has arrived before it starts, and from then on the buffer holds
        # that block as a cushion.
        self.prime_bytes = min(int(RATE * prime_ms / 1000) * LINK_BYTES_PER_FRAME,
                               self.ring_bytes)
        self.primed = True
        self.prime_started = 0.0
        self.port = port
        self.status_path = status_path
        self.listen_host = listen_host
        self.lock = threading.Lock()
        self.ring = bytearray()
        self.conn = None
        self.conn_id = 0
        self.running = True
        self.source_live = False
        self.frames_out = 0
        self.silence_frames = 0
        self.gap_bytes = 0
        self.reconnects = 0
        self.errors = 0
        self.last_error = ""
        self.started = time.time()

    # -- source side ----------------------------------------------------
    def _accept_loop(self) -> None:
        while self.running:
            try:
                conn, _ = self.listener.accept()
            except OSError:
                return
            with self.lock:
                previous = self.conn
                self.conn_id += 1
                self.conn = conn
                self.source_live = True
                self.reconnects += 1
                self.primed = False
                self.prime_started = time.monotonic()
                # A new source replaces the old one.  Whatever the old source
                # left in the buffer is dropped, so the new source starts
                # from silence instead of from stale audio.
                self.gap_bytes += len(self.ring)
                self.ring = bytearray()
            if previous is not None:
                try:
                    previous.close()
                except OSError:
                    pass
            threading.Thread(target=self._read_loop, args=(conn, self.conn_id),
                             daemon=True).start()

    def _read_loop(self, conn: socket.socket, conn_id: int) -> None:
        try:
            while self.running:
                data = conn.recv(65536)
                if not data:
                    break
                with self.lock:
                    if conn_id != self.conn_id:
                        return
                    self.ring.extend(data)
                    if len(self.ring) > self.ring_bytes:
                        excess = len(self.ring) - self.ring_bytes
                        del self.ring[:excess]
                        self.gap_bytes += excess
        except OSError:
            pass
        finally:
            with self.lock:
                if conn_id == self.conn_id:
                    self.gap_bytes += len(self.ring)
                    self.ring = bytearray()
                    self.conn = None
                    self.source_live = False

    # -- device side ----------------------------------------------------
    def _take(self):
        with self.lock:
            if not self.primed:
                if len(self.ring) >= self.prime_bytes:
                    self.primed = True
                elif time.monotonic() - self.prime_started >= PRIME_TIMEOUT_S:
                    # The source is not going to fill the buffer.  Start
                    # anyway instead of staying silent for good.
                    self.primed = True
                else:
                    return b"\x00" * BLOCK_BYTES, True
            if self.ring:
                count = min(len(self.ring), BLOCK_BYTES)
                data = bytes(self.ring[:count])
                del self.ring[:count]
                return data, False
        return b"\x00" * BLOCK_BYTES, True

    def _write_safely(self, data: bytes) -> None:
        delay = 0.5
        while self.running:
            try:
                self.sink.write(data)
                return
            except Exception as exc:  # keep the device side alive
                if not self.running:
                    # The process is on its way out and the signal that asked
                    # for it interrupted the write.  The device is closed on
                    # the way out, so there is nothing to recover here.
                    break
                self.errors += 1
                self.last_error = str(exc)
                try:
                    if self.sink.recover():
                        continue
                except Exception:
                    pass
                try:
                    self.sink.close()
                except Exception:
                    pass
                time.sleep(delay)
                delay = min(delay * 2, 5.0)
                try:
                    self.sink.open()
                except Exception as open_exc:
                    self.last_error = str(open_exc)

    def _status(self) -> dict:
        with self.lock:
            return {
                "state": "running",
                "sink": self.sink.name,
                "port": self.port,
                "format": "s16le",
                "rate": RATE,
                "link_channels": LINK_CHANNELS,
                "sink_channels": self.sink.channels,
                "source_live": self.source_live,
                "ring_bytes": self.ring_bytes,
                "ring_used": len(self.ring),
                "prime_bytes": self.prime_bytes,
                "primed": self.primed,
                "frames_out": self.frames_out,
                "silence_frames": self.silence_frames,
                "gap_bytes": self.gap_bytes,
                "reconnects": self.reconnects,
                "errors": self.errors,
                "last_error": self.last_error,
                "started": self.started,
                "pid": os.getpid(),
            }

    def _write_status(self) -> None:
        if not self.status_path:
            return
        payload = json.dumps(self._status(), indent=2, sort_keys=True)
        tmp = self.status_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as handle:
            handle.write(payload)
        os.replace(tmp, self.status_path)

    def run(self) -> int:
        self.sink.open()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind((self.listen_host, self.port))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self._accept_loop, daemon=True).start()

        next_status = 0.0
        while self.running:
            data, silent = self._take()
            self._write_safely(data)
            with self.lock:
                self.frames_out += len(data) // LINK_BYTES_PER_FRAME
                if silent:
                    self.silence_frames += len(data) // LINK_BYTES_PER_FRAME
            now = time.monotonic()
            if now >= next_status:
                self._write_status()
                next_status = now + 0.5
        return 0

    def stop(self, *_args) -> None:
        self.running = False
        try:
            if self.conn is not None:
                self.conn.close()
        except OSError:
            pass
        try:
            self.listener.close()
        except (OSError, AttributeError):
            pass


def build_sink(args) -> Sink:
    if args.sink == "alsa":
        return AlsaSink(args.device, args.latency_us, args.channels)
    return FileSink(args.out)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sink", choices=("alsa", "file"), default="file")
    parser.add_argument("--device", default="hw:Loopback,0",
                        help="ALSA playback device for the alsa sink")
    parser.add_argument("--out", default="auxmic-out.wav",
                        help="WAV file for the file sink")
    parser.add_argument("--port", type=int, default=0,
                        help="TCP port to listen on, 0 picks a free one")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--ring-ms", type=int, default=750)
    parser.add_argument("--prime-ms", type=int, default=500,
                        help="how much audio to hold back before the first block plays")
    parser.add_argument("--latency-us", type=int, default=100000)
    parser.add_argument("--channels", type=int, default=2,
                        help="channel count of the ALSA device; the mono link "
                             "stream is copied into every channel")
    parser.add_argument("--status", default="", help="status file path")
    args = parser.parse_args(argv)

    pacer = Pacer(build_sink(args), ring_ms=args.ring_ms, port=args.port,
                  status_path=args.status, listen_host=args.host)

    def handle_signal(*_args):
        pacer.stop()

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    try:
        return pacer.run()
    finally:
        pacer.stop()
        try:
            pacer.sink.close()
        except Exception:
            pass
        pacer._write_status()


if __name__ == "__main__":
    sys.exit(main())
