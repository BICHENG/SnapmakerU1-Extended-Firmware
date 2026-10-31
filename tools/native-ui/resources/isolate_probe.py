"""Device-side runner; root invokes this only after reviewing the probe."""
import ctypes, hashlib, json, os, signal, subprocess, sys, urllib.request
from pathlib import Path
stage = Path(sys.argv[1]).resolve()
assert stage.parent == Path('/userdata/ui-validation') and stage.name.startswith('native-resources-')
query = 'http://127.0.0.1:7125/printer/objects/query?webhooks&print_stats&extruder&extruder1&extruder2&extruder3&heater_bed'
state = json.load(urllib.request.urlopen(query, timeout=5))['result']['status']
assert state['webhooks']['state'] == 'ready'
assert state['print_stats']['state'] in ('standby', 'complete', 'cancelled')
assert all(value.get('target', 0) == 0 for value in state.values())
assert hashlib.sha256(Path('/usr/bin/gui').read_bytes()).hexdigest() == '2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472'
pid = subprocess.check_output(['pgrep', '-x', 'gui'], text=True).strip()
assert pid.isdecimal(), 'Expected one production GUI process'
identity = (Path('/proc') / pid / 'stat').read_text().split()[21]
manifest = json.loads((stage / 'hashes.json').read_text())
assert set(manifest) == {'libnative-resources-probe.so'}
for name, digest in manifest.items():
    assert hashlib.sha256((stage / name).read_bytes()).hexdigest() == digest

child = r'''
import ctypes, os, signal, stat, sys
from pathlib import Path
stage = Path(sys.argv[1])
signal.alarm(30)
libc = ctypes.CDLL(None, use_errno=True)
def check(value, operation):
    if value: raise OSError(ctypes.get_errno(), operation)
def mount(source, target, kind=None, flags=0, data=None):
    encode = lambda value: None if value is None else str(value).encode()
    check(libc.mount(encode(source), encode(target), encode(kind), ctypes.c_ulong(flags), encode(data)), 'mount ' + target)
payload = (stage / 'libnative-resources-probe.so').read_bytes()
nodes = {name: os.stat('/dev/' + name).st_rdev for name in ('null', 'zero', 'random', 'urandom')}
os.chdir('/')
check(libc.unshare(0x00020000 | 0x40000000), 'unshare mount/network')
mount(None, '/', flags=(1 << 18) | 16384)
for target in ('/oem', '/userdata', '/run', '/dev'):
    mount('tmpfs', target, 'tmpfs', data='size=4m,mode=755')
Path('/tmp').mkdir(parents=True, exist_ok=True)
mount('tmpfs', '/tmp', 'tmpfs', data='size=32m,mode=1777')
library = Path('/tmp/libnative-resources-probe.so')
library.write_bytes(payload); library.chmod(0o644)
for name, device in nodes.items():
    os.mknod('/dev/' + name, stat.S_IFCHR | 0o666, device); os.chmod('/dev/' + name, 0o666)
os.symlink('/proc/self/fd', '/dev/fd')
for target in ('/sys', '/etc'):
    mount(target, target, flags=4096)
    mount(None, target, flags=4096 | 32 | 1)
for absent in ('/dev/fb0', '/dev/input', '/dev/mali0', '/dev/dri', '/dev/dma_heap', '/home/lava/printer_data/comms/moonraker.sock'):
    assert not Path(absent).exists(), absent
check(libc.prctl(38, 1, 0, 0, 0), 'no_new_privileges')
os.setgroups([]); os.setgid(65534); os.setuid(65534)
ctypes.CDLL(str(library), mode=os.RTLD_NOW)
environment = {'PATH': '/usr/bin:/bin', 'HOME': '/tmp', 'TMPDIR': '/tmp',
    'U1_NATIVE_RESOURCES_ISOLATED_PROBE': '1', 'LD_PRELOAD': str(library)}
os.execve('/usr/bin/gui', ['/usr/bin/gui'], environment)
'''
process = subprocess.Popen([sys.executable, '-c', child, str(stage)], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, start_new_session=True)
stdout = stderr = ''
try:
    stdout, stderr = process.communicate(timeout=35)
finally:
    try: os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError: pass
    process.wait()
    (stage / 'stdout.txt').write_text(stdout)
    (stage / 'stderr.txt').write_text(stderr)
    assert identity == (Path('/proc') / pid / 'stat').read_text().split()[21], 'Production GUI changed'
if process.returncode: raise RuntimeError(stdout + stderr)
result = json.loads(stdout)
assert result['ok'] and result['case_count'] == 8
result.update(production_gui_pid=int(pid), production_gui_unchanged=True,
              isolated_mount_and_network=True, hardware_devices_available=False,
              unprivileged_uid=65534, probe_sha256=manifest['libnative-resources-probe.so'])
(stage / 'result.json').write_text(json.dumps(result, indent=2))
print(json.dumps({key: result[key] for key in ('ok', 'case_count', 'production_gui_unchanged', 'probe_sha256')}))
