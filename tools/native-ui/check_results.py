"""Check that published measurements retain their source evidence and boundaries."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
read = lambda path: json.loads(path.read_text())
results = read(root / 'results.json')
for name, digest in results['evidence_sha256'].items():
    assert Path(name).name == name
    assert hashlib.sha256((root / 'evidence' / name).read_bytes()).hexdigest() == digest, name
home = read(root / 'evidence/homepage.json')
assert results['homepage']['drawn_fps'] == [window['actual_fps'] for window in home['windows']]
assert all(window['complete'] and not window['idle_refreshes'] for window in home['windows'])
assert home['production_libraries_restored'] and not home['rendering_replaced'] and not home['input_sent']
off = read(root / 'evidence/homepage-off.json')
assert results['homepage']['baseline_33ms']['drawn_fps'] == [window['actual_fps'] for window in off['windows']]
assert off['production_libraries_restored'] and off['native_timer_policy_only'] and off['gpu_driver_unchanged']
compose = read(root / 'evidence/compose.json')
assert len(compose['cases']) == results['compositor_probe']['passed_exact_fetch_cases']
assert compose['ok'] and all(case['fetch']['pixels']['exact'] for case in compose['cases'])
resources = read(root / 'evidence/resources.json')
assert resources['ok'] and resources['case_count'] == results['resource_probe']['passed_cases']
assert not results['replacement_ui_implemented'] and not results['live_resource_capture_connected']
print('Published native drawing evidence verified')
