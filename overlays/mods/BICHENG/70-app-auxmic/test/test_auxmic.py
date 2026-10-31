"""Run with python -m unittest discover -s <this directory> -v."""
import ctypes
import json
from pathlib import Path
import struct
import sys
import threading
import time
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

BIN = Path(__file__).resolve().parents[1] / "root/usr/local/bin"
sys.path.insert(0, str(BIN))
import auxmic
from auxmic_pcm import AlsaOutput


class RecordingOutput:
    def __init__(self):
        self.data = bytearray()
        self.closed = False

    def open(self):
        pass

    def write(self, data):
        self.data.extend(data)
        time.sleep(0.01)

    def close(self):
        self.closed = True


class BufferTests(unittest.TestCase):
    def setUp(self):
        self.mic = auxmic.Microphone(RecordingOutput())
        self.sid, self.token = self.mic.create({})

    def push(self, payload, seq=0):
        return self.mic.accept(self.sid, self.token, seq, len(payload) // 2,
                               48000, 1, payload)

    def test_pcm_bytes_and_stop(self):
        payload = struct.pack("<h", 3210) * 48000
        self.push(payload)
        self.assertEqual(b"".join(self.mic.next_block() for _ in range(100)), payload)
        self.push(payload, 1)
        self.mic.stop(self.sid, self.token)
        self.assertEqual(self.mic.next_block(), bytes(960))
        self.assertEqual(self.mic.registry()["buffered_frames"], 0)

    def test_replaced_source_cannot_write_or_stop_new_source(self):
        old_sid, old_token = self.sid, self.token
        self.push(struct.pack("<h", 111) * 48000)
        self.sid, self.token = self.mic.create({"replace_existing": True})
        payload = struct.pack("<h", 222) * 48000
        self.push(payload)
        with self.assertRaises(auxmic.ServiceError) as caught:
            self.mic.accept(old_sid, old_token, 1, 1, 48000, 1, b"\xff\x7f")
        self.assertEqual(caught.exception.status, 404)
        with self.assertRaises(auxmic.ServiceError):
            self.mic.stop(old_sid, old_token)
        self.assertEqual(b"".join(self.mic.next_block() for _ in range(100)), payload)

    def test_overflow_drops_oldest_complete_samples(self):
        packets = [struct.pack("<h", value) * 48000 for value in (111, 222, 333)]
        for seq, payload in enumerate(packets):
            self.push(payload, seq)
        self.assertEqual(self.mic.registry()["buffered_frames"], 96000)
        self.assertEqual(self.mic.registry()["dropped_frames"], 48000)
        self.assertEqual(b"".join(self.mic.next_block() for _ in range(200)), b"".join(packets[1:]))

    def test_expiry_is_driven_by_audio_loop_without_status_polling(self):
        self.push(struct.pack("<h", 111) * 24000)
        with patch.object(auxmic.time, "monotonic", return_value=self.mic.last_activity + 11):
            self.assertEqual(self.mic.next_block(), bytes(960))
            self.assertEqual(self.mic.status(self.sid, self.token)["state"], "EXPIRED")

    def test_short_tail_is_not_stranded_while_waiting_for_a_full_buffer(self):
        payload = struct.pack("<h", 4321) * 300
        self.push(payload)
        self.assertEqual(self.mic.next_block(), bytes(960))
        with patch.object(auxmic.time, "monotonic", return_value=self.mic.prime_started + 2.01):
            self.assertEqual(self.mic.next_block(), payload + bytes(360))
        self.assertEqual(self.mic.registry()["buffered_frames"], 0)

    def test_bad_format_and_duplicates_do_not_enter_buffer(self):
        with self.assertRaises(auxmic.ServiceError):
            self.mic.accept(self.sid, self.token, 0, 1, 44100, 1, b"\x01\x00")
        with self.assertRaises(auxmic.ServiceError):
            self.mic.accept(self.sid, self.token, 0, 2, 48000, 1, b"\x01\x00")
        self.assertEqual(self.mic.registry()["buffered_frames"], 0)
        self.push(b"\x01\x00")
        with self.assertRaises(auxmic.ServiceError) as caught:
            self.push(b"\x02\x00")
        self.assertEqual(caught.exception.status, 409)
        self.assertEqual(self.mic.registry()["buffered_frames"], 1)

    def test_delayed_first_chunk_still_gets_a_full_priming_period(self):
        with patch.object(auxmic.time, "monotonic", return_value=self.mic.last_activity + 5):
            self.push(struct.pack("<h", 1234) * 12000)
            self.assertEqual(self.mic.next_block(), bytes(960))

    def test_measured_network_jitter_preserves_a_continuous_signal(self):
        arrivals = [1.609, 1.828, 2.093, 2.328, 2.625, 3.000, 3.062, 3.343,
                    3.578, 3.875, 4.078, 4.343, 4.593, 4.843, 5.093, 5.484,
                    5.593, 5.843, 6.109, 6.328, 6.609, 6.812, 7.062, 7.328,
                    7.578, 8.047, 8.359, 8.422, 8.625, 9.125, 9.234, 9.422]
        packets = [struct.pack("<h", seq + 1) * 12000 for seq in range(len(arrivals))]
        output = bytearray()
        start, seq = self.mic.last_activity, 0
        for tick in range(1200):
            elapsed = tick / 100
            with patch.object(auxmic.time, "monotonic", return_value=start + elapsed):
                while seq < len(arrivals) and arrivals[seq] <= elapsed:
                    self.push(packets[seq], seq)
                    seq += 1
                output.extend(self.mic.next_block())
        self.assertIn(b"".join(packets), bytes(output))
        self.assertEqual(self.mic.dropped_frames, 0)


class AlsaTests(unittest.TestCase):
    def test_partial_write_and_recovery_preserve_stereo_samples(self):
        received, errors = bytearray(), []
        results = iter((1, -32, 2))

        class Library:
            def snd_pcm_writei(self, pcm, pointer, frames):
                count = next(results)
                if count > 0:
                    received.extend(ctypes.string_at(pointer, count * 4))
                return count

            def snd_pcm_recover(self, pcm, code, silent):
                errors.append(code)
                return 0

        output = AlsaOutput()
        output.lib = Library()
        output.write(struct.pack("<hhh", 100, -200, 300))
        self.assertEqual(received, struct.pack("<hhhhhh", 100, 100, -200, -200, 300, 300))
        self.assertEqual(errors, [-32])

    def test_zero_progress_is_an_error(self):
        class Library:
            def snd_pcm_writei(self, *args):
                return 0
        output = AlsaOutput()
        output.lib = Library()
        with self.assertRaises(OSError):
            output.write(b"\x01\x00")


class LifecycleTests(unittest.TestCase):
    def test_fatal_output_failure_propagates_and_releases_the_listener(self):
        class FailedOutput(RecordingOutput):
            def write(self, data):
                raise OSError("device disconnected")

        output = FailedOutput()
        microphone = auxmic.Microphone(output)
        server = auxmic.AuxmicServer(("127.0.0.1", 0), microphone, "test", "unused")
        with self.assertRaisesRegex(OSError, "device disconnected"):
            auxmic.serve(server)
        self.assertTrue(output.closed)
        self.assertFalse(microphone.device_ready)
        self.assertEqual(server.socket.fileno(), -1)


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.output = RecordingOutput()
        self.mic = auxmic.Microphone(self.output)
        self.page = BIN.parent / "share/auxmic/phone.html"
        self.server = auxmic.AuxmicServer(("127.0.0.1", 0), self.mic, "aabbccdd", self.page)
        self.base = "http://127.0.0.1:%d" % self.server.server_port
        self.worker = threading.Thread(target=auxmic.serve, args=(self.server,), daemon=True)
        self.worker.start()

    def tearDown(self):
        self.mic.stopping.set()
        self.worker.join(3)
        self.assertFalse(self.worker.is_alive())
        self.assertTrue(self.output.closed)

    def request(self, path, body=None, token=None, headers=None):
        request_headers = headers or {}
        if token is not None:
            request_headers["Authorization"] = "Bearer " + token
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            request_headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self.base + path, body, request_headers)
        try:
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    def create(self):
        code, body = self.request("/v1/mic/session", {
            "pairing": "AABBCCDD", "audio_context_sample_rate": 48000,
            "replace_existing": True,
        })
        self.assertEqual(code, 201)
        return json.loads(body)

    def test_phone_protocol_reaches_output_and_beacon_stops_it(self):
        session = self.create()
        payload = struct.pack("<h", 1234) * 12000
        for seq in range(4):
            code, _ = self.request(session["chunk_url"], payload, session["token"], {
                "X-Seq": str(seq), "X-Sample-Count": "12000",
                "X-Sample-Rate": "48000", "X-Channels": "1",
            })
            self.assertEqual(code, 200)
        deadline = time.monotonic() + 2
        while bytes(self.output.data).find(payload * 4) < 0 and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertIn(payload * 4, bytes(self.output.data))
        code, _ = self.request(session["stop_url"], {"token": session["token"]})
        self.assertEqual(code, 200)
        time.sleep(0.1)
        self.assertEqual(self.output.data[-4800:], bytes(4800))

    def test_pairing_auth_and_original_page(self):
        code, _ = self.request("/v1/mic/session", {"pairing": "wrong"})
        self.assertEqual(code, 403)
        session = self.create()
        code, _ = self.request(session["status_url"], token="wrong")
        self.assertEqual(code, 401)
        code, body = self.request(session["status_url"], token=session["token"])
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(body)["state"], "OPEN")
        code, body = self.request("/")
        self.assertEqual(code, 200)
        self.assertEqual(body, self.page.read_bytes())

    def test_idle_session_is_expired_without_client_requests(self):
        self.mic.idle_timeout_s = 0.05
        session = self.create()
        time.sleep(0.1)
        code, body = self.request(session["status_url"], token=session["token"])
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(body)["state"], "EXPIRED")


if __name__ == "__main__":
    unittest.main()
