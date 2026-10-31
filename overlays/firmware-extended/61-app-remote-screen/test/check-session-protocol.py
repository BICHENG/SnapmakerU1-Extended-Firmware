#!/usr/bin/env python3
"""Exercise the patched signalling handlers with a fake RTC boundary, without a printer."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True, help='Pinned v4l2-mpp apps/stream-webrtc/main.cpp')
p.add_argument('--json-include', type=Path, required=True, help='libdatachannel/deps/json/include')
a = p.parse_args()
folder = Path(__file__).resolve().parent

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    source = tmp / 'main.cpp'
    source.write_text(a.source.read_text())
    subprocess.run([sys.executable, str(folder.parent / 'apps/patch_webrtc.py'), str(source)], check=True)
    patched = source.read_text()

    def section(start, end):
        return patched[patched.index(start):patched.index(end)]

    functions = section('static std::shared_ptr<Client> find_client', 'static bool has_clients')
    functions += section('static void cleanup_clients', 'static void ping_clients')
    functions += section('static size_t client_count', 'static std::shared_ptr<Client> create_client')
    # Keep the production identity allocation; replace only RTC setup with a fake peer.
    functions += section('static std::shared_ptr<Client> create_client', '    rtc::Configuration config;')
    functions += '''    client->pc = std::make_shared<rtc::PeerConnection>();
    g_clients.push_back(client);
    return client;
}
'''
    functions += section('static void close_screen_clients', 'static void handle_connection')
    (tmp / 'protocol-under-test.inc').write_text(functions)
    executable = tmp / 'session-protocol-test'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(tmp), '-I' + str(a.json_include),
                    str(folder / 'session-protocol-test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    touch_test = tmp / 'touch-lease-test'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-pthread', '-I' + str(a.json_include), str(folder / 'touch-lease-test.cpp'),
                    '-o', str(touch_test)], check=True)
    subprocess.run([str(touch_test)], check=True)
