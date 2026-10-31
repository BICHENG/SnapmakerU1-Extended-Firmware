"""Device-side supervisor: only the child receives GPU nodes; production stays mounted normally."""
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import urllib.request

KERNEL_ERROR_PATTERN = r'(?i)(?:\bmali\b.*(?:oom|fault|error|terminat)|\bGPU\b.*(?:fault|error|terminat)|page allocation failure|TRANSLATION_FAULT|OOM notifier|oom[-_]kill|Out of memory|Killed process)'


def kernel_delta(before_text, after_text):
    before_lines, after_lines = before_text.splitlines(), after_text.splitlines()
    timestamps = lambda lines: [float(match.group(1)) for line in lines
                               if (match := re.match(r'^\[\s*(\d+(?:\.\d+)?)\]', line))]
    before_times, after_times = timestamps(before_lines), timestamps(after_lines)
    cursor = before_lines[-1] if before_lines else ''
    positions = [index for index, line in enumerate(after_lines) if cursor and line == cursor]
    complete = bool(positions and before_times and after_times and after_times[-1] >= before_times[-1])
    delta = after_lines[positions[-1] + 1:] if positions else after_lines
    errors = [line for line in delta if re.search(KERNEL_ERROR_PATTERN, line)]
    return dict(source='dmesg', timestamp_unit='seconds_since_boot', before_cursor=cursor,
                before_last_timestamp=before_times[-1] if before_times else None,
                after_last_timestamp=after_times[-1] if after_times else None,
                delta_complete=complete, delta_lines=delta, error_pattern=KERNEL_ERROR_PATTERN,
                errors=errors, passed=complete and not errors)


def mali_allocations_kb(contents, page_size):
    per_pid = {}
    total = None
    for line in contents.splitlines():
        fields = line.split()
        if fields and fields[0] == 'mali0':
            total = int(fields[1]) * page_size // 1024
        elif fields and fields[0].startswith('kctx-'):
            process_id, pages = map(int, fields[-2:])
            per_pid[process_id] = per_pid.get(process_id, 0) + pages * page_size // 1024
    assert total is not None, 'Mali memory table has no device total'
    return total, per_pid


stage = Path(sys.argv[1])
mode = 'compose'
iterations = int(sys.argv[2])
assert 1 <= iterations <= 100
state = json.load(urllib.request.urlopen(
    'http://127.0.0.1:7125/printer/objects/query?webhooks&print_stats&extruder&extruder1&extruder2&extruder3&heater_bed',
    timeout=5))['result']['status']
assert state['webhooks']['state'] == 'ready'
assert state['print_stats']['state'] in ('standby', 'complete', 'cancelled')
assert all(value.get('target', 0) == 0 for value in state.values())
gpu = Path('/sys/class/devfreq/ff320000.gpu')
assert (gpu / 'max_freq').read_text().strip() == '400000000'
deadline = time.monotonic() + 5
while (gpu / 'device/power/runtime_status').read_text().strip() != 'suspended':
    assert time.monotonic() < deadline, 'GPU already in use'
    time.sleep(0.2)
pid = subprocess.check_output(['pgrep', '-x', 'gui'], text=True).strip()
assert pid.isdigit()
before = (Path('/proc') / pid / 'stat').read_text().split()[21]
manifest = json.loads((stage / 'source.json').read_text())
binary = stage / 'libnative-compose-probe.so'
assert hashlib.sha256(Path('/usr/bin/gui').read_bytes()).hexdigest() == '2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472'
assert manifest['binary']['name'] == binary.name
binary_hash = hashlib.sha256()
with binary.open('rb') as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        binary_hash.update(block)
assert binary_hash.hexdigest() == manifest['binary']['sha256']
child = r'''
import base64,ctypes,json,os,signal,stat,subprocess,sys
from pathlib import Path
stage=Path(sys.argv[1]);iterations=int(sys.argv[2]);mode='compose'
libc=ctypes.CDLL(None,use_errno=True)
def check(result, operation):
    if result:raise OSError(ctypes.get_errno(),operation)
def mount(source,target,kind=None,flags=0,data=None):
    encode=lambda value:None if value is None else str(value).encode()
    check(libc.mount(encode(source),encode(target),encode(kind),ctypes.c_ulong(flags),encode(data)), 'mount '+str(target))
original_net=os.readlink('/proc/self/ns/net')
check(libc.unshare(0x00020000|0x40000000),'unshare mount/network')
mount(None,'/',flags=(1<<18)|16384)
nodes={name:os.stat('/dev/'+name).st_rdev for name in ('null','zero','random','urandom')}
if mode!='off':
    nodes['mali0']=os.stat('/dev/mali0').st_rdev
    for node in Path('/dev/dri').glob('renderD*'):
        if node.is_char_device():nodes['dri/'+node.name]=node.stat().st_rdev
    for name in ('system','system-uncached'):
        node=Path('/dev/dma_heap')/name
        if node.exists():nodes['dma_heap/'+name]=node.stat().st_rdev
mount('tmpfs','/tmp','tmpfs',data='size=64m,mode=1777')
private=Path('/tmp/compose-test');private.mkdir(mode=0o755);private.chmod(0o755)
library=private/'libnative-compose-probe.so';library.touch()
mount(stage/library.name,library,flags=4096)
mount(None,library,flags=4096|32|1)
arguments=['/usr/bin/gui','--output',str(private/'output'),'--iterations',str(iterations)]
for target in ('/oem','/userdata','/home','/root','/run','/dev'):
    if Path(target).exists():mount('tmpfs',target,'tmpfs',data='size=4m,mode=755')
for directory in ('dri','dma_heap','char','shm'):
    path=Path('/dev')/directory;path.mkdir();path.chmod(0o1777 if directory=='shm' else 0o755)
for name,device in nodes.items():
    path='/dev/'+name;os.mknod(path,stat.S_IFCHR|0o666,device);os.chmod(path,0o666)
    os.symlink('../'+name,'/dev/char/'+str(os.major(device))+':'+str(os.minor(device)))
os.symlink('/proc/self/fd','/dev/fd')
for target in ('/sys','/etc'):
    mount(target,target,flags=4096)
    mount(None,target,flags=4096|32|1)
assert not Path('/dev/fb0').exists() and not Path('/dev/input').exists()
assert not list(Path('/dev/dri').glob('card*'))
assert not Path('/home/lava/printer_data/comms/moonraker.sock').exists()
assert os.readlink('/proc/self/ns/net')!=original_net
output=private/'output';output.mkdir();os.chown(output,65534,65534)
check(libc.prctl(38,1,0,0,0),'no_new_privileges')
os.setgroups([]);os.setgid(65534);os.setuid(65534)
ctypes.CDLL(str(library),mode=os.RTLD_NOW)
environment={'PATH':'/usr/bin:/bin','HOME':str(output),'TMPDIR':'/tmp',
             'U1_VENDOR_DRAW_PROBE':'1','LD_PRELOAD':str(library)}
running=subprocess.Popen(arguments,env=environment,cwd=private,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    test_start_tick=int((Path('/proc')/str(running.pid)/'stat').read_text().split()[21])
except FileNotFoundError:
    test_start_tick=None
try:
    stdout,stderr=running.communicate(timeout=45)
except BaseException:
    running.kill();running.wait();raise
completed=subprocess.CompletedProcess(arguments,running.returncode,stdout,stderr)
files={}
for name in ('result.json',)+tuple(scene+'-'+mode+'.ppm' for scene in ('selection','opacity_128','dense_page') for mode in ('native','standard','fetch')):
    path=output/name
    if path.is_file():files[name]=base64.b64encode(path.read_bytes()).decode()
print(json.dumps({'returncode':completed.returncode,'stdout':completed.stdout,'stderr':completed.stderr,
                  'files':files,'uid':os.getuid(),'private_network':True,'framebuffer_and_input_hidden':True,
                  'primary_drm_cards_hidden':True,'gpu_devices_visible':mode!='off','read_only_input':True,
                  'test_pid':running.pid,'test_start_tick':test_start_tick}))
'''
kernel_before = subprocess.check_output(['dmesg'], text=True)
process = subprocess.Popen([sys.executable, '-c', child, str(stage), str(iterations)], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, start_new_session=True)
minimum_available_kb = None
peak_test_mali_kb = peak_total_mali_kb = 0
page_size = os.sysconf('SC_PAGE_SIZE')
memory_samples = []
test_pid = test_start_tick = None
test_mali_samples = 0
failure = None
stdout = stderr = ''
started = time.monotonic()
deadline = time.monotonic() + 60
try:
    while True:
        available = next(int(line.split()[1]) for line in Path('/proc/meminfo').read_text().splitlines()
                         if line.startswith('MemAvailable:'))
        minimum_available_kb = min(available, minimum_available_kb or available)
        total_mali_kb, per_pid = mali_allocations_kb(
            Path('/sys/kernel/debug/mali0/gpu_memory').read_text(), page_size)
        test_mali_kb = per_pid.get(test_pid, 0)
        test_mali_samples += test_pid in per_pid
        peak_test_mali_kb = max(peak_test_mali_kb, test_mali_kb)
        peak_total_mali_kb = max(peak_total_mali_kb, total_mali_kb)
        memory_samples.append({'elapsed_ms': round((time.monotonic() - started) * 1000, 1),
                               'available_kb': available, 'test_pid': test_pid,
                               'test_mali_allocated_kb': test_mali_kb,
                               'total_mali_allocated_kb': total_mali_kb,
                               'mali_allocated_kb_by_pid': per_pid})
        if available < 128 * 1024:
            failure = 'Stopped test before memory exhaustion: available ' + str(available) + ' kB'
            break
        if time.monotonic() >= deadline:
            failure = 'Isolated test timed out'
            break
        try:
            stdout, stderr = process.communicate(timeout=0.1)
            break
        except subprocess.TimeoutExpired:
            pass
except Exception as error:
    failure = repr(error)
finally:
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    stdout, stderr = process.communicate()
    gui_after = (Path('/proc') / pid / 'stat').read_text().split()[21]
    gui_unchanged = before == gui_after
    kernel_evidence = kernel_delta(kernel_before, subprocess.check_output(['dmesg'], text=True))
destination = stage / mode
destination.mkdir(exist_ok=True)
(destination / 'supervisor.stderr').write_text(stderr)
(destination / 'kernel-errors.json').write_text(json.dumps(kernel_evidence, indent=2))
(destination / 'memory-samples.json').write_text(json.dumps(memory_samples, indent=2))
metadata = dict(production_gui_pid=int(pid), production_gui_unchanged=gui_unchanged,
                production_gui_start_tick_before=int(before), production_gui_start_tick_after=int(gui_after),
                kernel_errors_passed=kernel_evidence['passed'],
                test_binary_sha256=manifest['binary']['sha256'], printer_state=state['print_stats']['state'],
                minimum_available_kb=minimum_available_kb, test_pid=test_pid, test_start_tick=test_start_tick,
                sampled_peak_test_mali_allocated_kb=peak_test_mali_kb if test_mali_samples or mode == 'off' else None,
                sampled_peak_total_mali_allocated_kb=peak_total_mali_kb,
                memory_sampling_target_interval_ms=100, memory_sample_count=len(memory_samples),
                test_mali_sample_count=test_mali_samples, system_page_size=page_size,
                mali_memory_source='/sys/kernel/debug/mali0/gpu_memory')
if failure or process.returncode != 0 or not gui_unchanged:
    metadata.update(returncode=process.returncode, supervisor_failure=failure or stderr or 'Production GUI changed')
    (destination / 'isolation.json').write_text(json.dumps(metadata, indent=2))
    raise AssertionError(metadata['supervisor_failure'])
packet = json.loads(stdout)
test_pid = packet['test_pid']
test_start_tick = packet['test_start_tick']
for sample in memory_samples:
    sample['test_pid'] = test_pid
    sample['test_mali_allocated_kb'] = sample['mali_allocated_kb_by_pid'].get(test_pid, 0)
test_mali_samples = sum(test_pid in sample['mali_allocated_kb_by_pid'] for sample in memory_samples)
peak_test_mali_kb = max((sample['test_mali_allocated_kb'] for sample in memory_samples), default=0)
metadata.update(test_pid=test_pid, test_start_tick=test_start_tick, test_mali_sample_count=test_mali_samples,
                sampled_peak_test_mali_allocated_kb=peak_test_mali_kb if test_mali_samples or mode == 'off' else None)
(destination / 'memory-samples.json').write_text(json.dumps(memory_samples, indent=2))
for name, content in packet.pop('files').items():
    assert name in ('result.json',) + tuple(scene+'-'+mode+'.ppm' for scene in ('selection','opacity_128','dense_page') for mode in ('native','standard','fetch'))
    (destination / name).write_bytes(base64.b64decode(content, validate=True))
(destination / 'stdout.txt').write_text(packet.pop('stdout'))
(destination / 'stderr.txt').write_text(packet.pop('stderr'))
packet.update(metadata)
(destination / 'isolation.json').write_text(json.dumps(packet, indent=2))
assert kernel_evidence['passed'], 'New or incomplete kernel GPU/memory error evidence; see kernel-errors.json'
assert packet['returncode'] == 0, (destination / 'stderr.txt').read_text()
result = json.loads((destination / 'result.json').read_text())
assert result['ok'] and result['original_main_called'] is False
assert result['capabilities']['stride'] == 1920 and len(result['cases']) == 15
assert all(case['capture_and_replay_exact'] and case['cpu_rw_exact'] for case in result['cases'])
print(json.dumps({'mode': mode, 'isolation': packet, 'standard_exact': result['standard_all_pixels_exact'],
                  'fetch_exact': result['fetch_all_pixels_exact'], 'cases': len(result['cases'])}))
