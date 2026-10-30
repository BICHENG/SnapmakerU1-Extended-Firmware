# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
"""PCM outputs for the auxmic device service."""
import ctypes
import time
import wave

RATE = 48000
BLOCK_FRAMES = 480
BLOCK_BYTES = BLOCK_FRAMES * 2


class AlsaOutput:
    """Keep the loopback playback endpoint open, writing mono as stereo."""

    def __init__(self, device="hw:Loopback,0,0"):
        self.device = device
        self.pcm = ctypes.c_void_p()
        self.lib = None

    def open(self):
        self.lib = ctypes.CDLL("libasound.so.2")
        signatures = {
            "snd_pcm_open": ([ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p,
                              ctypes.c_int, ctypes.c_int], ctypes.c_int),
            "snd_pcm_set_params": ([ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_uint, ctypes.c_uint, ctypes.c_int,
                                    ctypes.c_uint], ctypes.c_int),
            "snd_pcm_writei": ([ctypes.c_void_p, ctypes.c_void_p,
                                ctypes.c_ulong], ctypes.c_long),
            "snd_pcm_recover": ([ctypes.c_void_p, ctypes.c_int,
                                 ctypes.c_int], ctypes.c_int),
            "snd_pcm_close": ([ctypes.c_void_p], ctypes.c_int),
            "snd_strerror": ([ctypes.c_int], ctypes.c_char_p),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result
        self._check(self.lib.snd_pcm_open(
            ctypes.byref(self.pcm), self.device.encode(), 0, 0))
        try:
            # S16_LE, interleaved, stereo, 48 kHz, 100 ms device latency.
            self._check(self.lib.snd_pcm_set_params(
                self.pcm, 2, 3, 2, RATE, 1, 100000))
        except Exception:
            self.close()
            raise

    def _check(self, code):
        if code < 0:
            raise OSError(code, self.lib.snd_strerror(code).decode("utf-8", "replace"))
        return code

    def write(self, mono):
        stereo = bytearray(len(mono) * 2)
        stereo[0::4] = mono[0::2]
        stereo[1::4] = mono[1::2]
        stereo[2::4] = mono[0::2]
        stereo[3::4] = mono[1::2]
        buffer = (ctypes.c_char * len(stereo)).from_buffer(stereo)
        frames, offset, recoveries = len(mono) // 2, 0, 0
        while offset < frames:
            written = self.lib.snd_pcm_writei(
                self.pcm, ctypes.byref(buffer, offset * 4), frames - offset)
            if written < 0:
                # Recover the actual ALSA error; preserve the unwritten frames.
                recoveries += 1
                if recoveries > 3:
                    self._check(written)
                self._check(self.lib.snd_pcm_recover(self.pcm, written, 1))
                continue
            if written == 0:
                raise OSError("ALSA made no write progress")
            offset += written

    def close(self):
        if self.lib is not None and self.pcm:
            self.lib.snd_pcm_close(self.pcm)
            self.pcm = ctypes.c_void_p()


class WaveOutput:
    """Use the same service on a development host without an ALSA device."""

    def __init__(self, path):
        self.path = path
        self.handle = None
        self.frames = 0

    def open(self):
        self.handle = wave.open(str(self.path), "wb")
        self.handle.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        self.started = time.monotonic()

    def write(self, data):
        self.handle.writeframes(data)
        self.frames += len(data) // 2
        delay = self.started + self.frames / RATE - time.monotonic()
        if delay > 0:
            time.sleep(delay)

    def close(self):
        if self.handle is not None:
            self.handle.close()
            self.handle = None
