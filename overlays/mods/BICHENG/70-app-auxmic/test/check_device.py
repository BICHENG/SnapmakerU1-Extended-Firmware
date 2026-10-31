"""Run on U1 to compare uploaded PCM with capture from the installed ALSA alias."""
import argparse
import contextlib
import http.client
import json
from pathlib import Path
import random
import ssl
import struct
import subprocess
import threading
import time


PREFIX = struct.pack('<3h', 12345, -23456, 137)


def packet(sequence):
    return PREFIX + struct.pack('<h', sequence + 1) + random.Random(sequence).randbytes(23992)


def stereo(mono):
    result = bytearray(len(mono) * 2)
    result[0::4] = result[2::4] = mono[0::2]
    result[1::4] = result[3::4] = mono[1::2]
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 1800:
        parser.error('--seconds must be between 1 and 1800')
    expected = args.seconds * 4
    received = {'packets': 0, 'mismatches': 0, 'sequence_gaps': 0, 'silent_tail': False}
    capture = subprocess.Popen([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'alsa', '-ar', '48000',
        '-ac', '2', '-i', 'auxmic', '-f', 's16le', '-flush_packets', '1', 'pipe:1',
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def collect():
        buffer = bytearray()
        marker, previous = stereo(PREFIX), -1
        while True:
            block = capture.stdout.read(1920)
            if not block:
                return
            received['silent_tail'] = not any(block)
            buffer.extend(block)
            while True:
                start = buffer.find(marker)
                if start < 0:
                    del buffer[:max(0, len(buffer) - len(marker) + 1)]
                    break
                if len(buffer) - start < 48000:
                    break
                sequence = struct.unpack_from('<h', buffer, start + 12)[0] - 1
                if sequence < 0 or bytes(buffer[start:start + 48000]) != stereo(packet(sequence)):
                    received['mismatches'] += 1
                if sequence != previous + 1:
                    received['sequence_gaps'] += 1
                previous = sequence
                received['packets'] += 1
                del buffer[:start + 48000]

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    client = http.client.HTTPSConnection('127.0.0.1', 9443, timeout=5,
                                         context=ssl._create_unverified_context())

    def request(method, path, body=None, headers=None):
        if isinstance(body, dict):
            body = json.dumps(body).encode()
        client.request(method, path, body, headers or {})
        response = client.getresponse()
        value = json.loads(response.read())
        if response.status >= 400:
            raise RuntimeError(value)
        return value

    session, failure, sent = None, None, 0
    try:
        time.sleep(.5)
        if capture.poll() is not None:
            raise RuntimeError(capture.stderr.read().decode())
        initial = request('GET', '/v1/auxmic/source')
        pairing = Path('/home/lava/printer_data/config/extended/auxmic/pairing.token').read_text().strip()
        # Refuse to take over an existing phone session.
        session = request('POST', '/v1/mic/session', {'pairing': pairing, 'audio_context_sample_rate': 48000})
        started = time.monotonic()
        for sequence in range(expected):
            delay = started + sequence * .25 - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            request('POST', session['chunk_url'], packet(sequence), {
                'Authorization': 'Bearer ' + session['token'], 'X-Seq': str(sequence),
                'X-Sample-Count': '12000', 'X-Sample-Rate': '48000', 'X-Channels': '1',
            })
            sent += 1
        time.sleep(2)
        final = request('GET', '/v1/auxmic/source')
        received['dropped_frames'] = final['dropped_frames'] - initial['dropped_frames']
    except Exception as error:
        failure = str(error)
    finally:
        if session:
            with contextlib.suppress(Exception):
                request('POST', session['stop_url'], {'token': session['token']})
            time.sleep(1)
        client.close()
        if capture.poll() is None:
            capture.terminate()
        capture.wait(timeout=5)
        reader.join(3)
    passed = (failure is None and not reader.is_alive() and sent == expected
              and received['packets'] == expected and not received['mismatches']
              and not received['sequence_gaps'] and not received.get('dropped_frames')
              and received['silent_tail'])
    print(json.dumps({'passed': passed, 'seconds': args.seconds, 'sent': sent,
                      'capture': received, 'failure': failure}, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
