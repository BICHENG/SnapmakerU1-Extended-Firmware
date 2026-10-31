#!/usr/bin/env python3
import json
import subprocess
import sys
from urllib.parse import urlencode
from urllib.request import urlopen


CONFIG = '/oem/printer_data/config/extended/extended2.cfg'
CONFIG_TOOL = '/usr/local/bin/extended-config.py'


def query_objects(objects):
    url = 'http://127.0.0.1:7125/printer/objects/query?' + urlencode(objects)
    with urlopen(url, timeout=3) as response:
        return json.load(response)['result']['status']


def require_idle():
    status = query_objects({'print_stats': 'state', 'idle_timeout': 'state',
                            'heaters': 'available_heaters'})
    if (status['print_stats']['state'] not in ('standby', 'complete', 'cancelled', 'error')
            or status['idle_timeout']['state'] not in ('Idle', 'Ready')):
        raise ValueError('Wait until the printer is idle and the print is no longer active or paused.')
    heaters = status['heaters']['available_heaters']
    if not isinstance(heaters, list) or not heaters or not all(isinstance(h, str) and h for h in heaters):
        raise ValueError('Cannot read the printer heater list.')
    targets = query_objects({heater: 'target' for heater in heaters})
    if any(targets[heater]['target'] != 0 for heater in heaters):
        raise ValueError('Turn off all heaters before changing GPU acceleration.')


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1 or args[0] not in ('true', 'false'):
        print('Usage: ui-gpu-setting.py true|false', file=sys.stderr)
        return 1
    try:
        require_idle()
        subprocess.run([CONFIG_TOOL, 'add', CONFIG, 'components', 'mali_gpu', args[0]], check=True)
        print('GPU acceleration setting saved. Rebooting...', flush=True)
        subprocess.Popen(['/bin/sh', '-c', 'sleep 3; /sbin/reboot'],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
