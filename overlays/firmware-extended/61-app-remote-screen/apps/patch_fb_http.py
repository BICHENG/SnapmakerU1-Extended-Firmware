#!/usr/bin/env python3
import sys
from pathlib import Path

path = Path(sys.argv[1])
source = path.read_text()
route = "        if parsed.path == '/touch':"
assert source.count(route) == 1
source = source.replace('import struct\n', 'import struct\nimport socket\nimport json\n')
source = source.replace(route, "        if parsed.path == '/webrtc':\n            self.handle_webrtc()\n        elif parsed.path == '/touch':")
handler = '''    def handle_webrtc(self):
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 65536:
                self.send_error(413)
                return
            request = json.loads(self.rfile.read(size))
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(5)
                sock.connect('/tmp/screen-webrtc.sock')
                sock.sendall(json.dumps(request).encode() + b'\\n')
                with sock.makefile('rb') as stream:
                    response = stream.readline(65537)
                if not response.endswith(b'\\n') or len(response) > 65536:
                    raise ValueError('Invalid signaling response')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(response)))
            self.end_headers()
            self.wfile.write(response)
        except (OSError, ValueError):
            self.send_error(503, 'Screen connection unavailable')

'''
anchor = '    def handle_snapshot(self):'
assert source.count(anchor) == 1
source = source.replace(anchor, handler + anchor)
compile(source, str(path), 'exec')
path.write_text(source)
