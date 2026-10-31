import configparser
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import URLError
from urllib.parse import parse_qs, urlparse

import yaml


OVERLAY = Path(__file__).parents[1]
REPO = OVERLAY.parents[3]
CONFIG_TOOL = REPO / 'overlays/firmware-extended/02-firmware-config/root/usr/local/bin/extended-config.py'
spec = importlib.util.spec_from_file_location('ui_gpu_setting', OVERLAY / 'root/usr/local/bin/ui-gpu-setting.py')
setting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setting)


class GpuSettingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'extended2.cfg'
        self.original = '[components]\ngui: helixscreen\nrfid: true\n\n[camera]\nmode: auto\n'
        self.config.write_text(self.original)
        self.config_tool = self.root / 'extended-config.py'
        shutil.copyfile(CONFIG_TOOL, self.config_tool)
        self.config_tool.chmod(0o755)
        self.status = {'print_stats': {'state': 'complete'}, 'idle_timeout': {'state': 'Ready'},
                       'heaters': {'available_heaters': ['extruder', 'extruder1', 'heater_bed']}}
        self.targets = {h: {'target': 0.0} for h in self.status['heaters']['available_heaters']}
        self.requests = []

    def response(self, url, timeout):
        self.assertEqual(timeout, 3)
        self.assertEqual(urlparse(url).netloc, '127.0.0.1:7125')
        query = parse_qs(urlparse(url).query)
        self.requests.append(query)
        status = self.status if 'print_stats' in query else self.targets
        return io.StringIO(json.dumps({'result': {'status': status}}))

    def apply(self, value, network_error=None):
        with patch.object(setting, 'CONFIG', str(self.config)), \
             patch.object(setting, 'CONFIG_TOOL', str(self.config_tool)), \
             patch.object(setting, 'urlopen', side_effect=network_error or self.response), \
             patch.object(setting.subprocess, 'Popen') as reboot, \
             redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            # subprocess.run uses Popen internally; let configuration writes execute normally.
            real_run = subprocess.run
            with patch.object(setting.subprocess, 'run', side_effect=lambda *a, **kw: self.run_config(real_run, *a, **kw)):
                result = setting.main([value])
        return result, reboot

    def run_config(self, real_run, *args, **kwargs):
        with patch.object(subprocess, 'Popen', self.real_popen):
            return real_run(*args, **kwargs)

    real_popen = subprocess.Popen

    def test_both_values_preserve_other_settings_and_schedule_reboot(self):
        for value in ('true', 'false'):
            with self.subTest(value=value):
                result, reboot = self.apply(value)
                self.assertEqual(result, 0)
                config = configparser.ConfigParser()
                config.read(self.config)
                self.assertEqual(dict(config['components']), {'gui': 'helixscreen', 'rfid': 'true', 'mali_gpu': value})
                self.assertEqual(dict(config['camera']), {'mode': 'auto'})
                reboot.assert_called_once_with(['/bin/sh', '-c', 'sleep 3; /sbin/reboot'],
                                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                              stderr=subprocess.DEVNULL, start_new_session=True)
                self.assertEqual(self.requests[-1], {h: ['target'] for h in self.targets})

    def test_active_or_unknown_print_does_not_write_or_reboot(self):
        for state in ('printing', 'paused', 'unknown'):
            with self.subTest(state=state):
                self.status['print_stats']['state'] = state
                result, reboot = self.apply('false')
                self.assertEqual(result, 1)
                self.assertEqual(self.config.read_text(), self.original)
                reboot.assert_not_called()

    def test_busy_or_heating_does_not_write_or_reboot(self):
        for busy in ('motion', 'heater'):
            with self.subTest(busy=busy):
                self.status['idle_timeout']['state'] = 'Printing' if busy == 'motion' else 'Idle'
                self.targets['extruder1']['target'] = 200 if busy == 'heater' else 0
                result, reboot = self.apply('true')
                self.assertEqual(result, 1)
                self.assertEqual(self.config.read_text(), self.original)
                reboot.assert_not_called()

    def test_missing_heater_or_unavailable_status_does_not_write_or_reboot(self):
        del self.targets['heater_bed']
        for error in (None, URLError('offline')):
            with self.subTest(error=error):
                result, reboot = self.apply('true', error)
                self.assertEqual(result, 1)
                self.assertEqual(self.config.read_text(), self.original)
                reboot.assert_not_called()

    def test_invalid_value_does_not_query_write_or_reboot(self):
        result, reboot = self.apply('enabled')
        self.assertEqual(result, 1)
        self.assertEqual(self.requests, [])
        self.assertEqual(self.config.read_text(), self.original)
        reboot.assert_not_called()

    def test_failed_config_write_does_not_reboot(self):
        self.config.unlink()
        result, reboot = self.apply('true')
        self.assertEqual(result, 1)
        self.assertFalse(self.config.exists())
        reboot.assert_not_called()

    def test_yaml_options_survive_frontend_string_round_trip(self):
        document = yaml.safe_load((OVERLAY / 'root/usr/local/share/firmware-config/functions/26_settings_ui_gpu.yaml').read_text())
        item = document['settings']['snapmaker_components']['items']['mali_gpu']
        self.assertEqual(item['default'], 'false')
        self.assertEqual(item['get_cmd'][-1], 'false')
        self.assertEqual(list(item['options']), ['false', 'true'])
        for key in json.loads(json.dumps(item['options'])):
            self.assertEqual(item['options'][key]['cmd'], ['/usr/bin/python3', '/usr/local/bin/ui-gpu-setting.py', key])


class GpuPolicyTest(unittest.TestCase):
    def test_policy_requires_config_device_and_confirmed_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / 'extended2.cfg'
            limit = root / 'max_freq'
            tool = root / 'extended-config.py'
            shutil.copyfile(CONFIG_TOOL, tool)
            tool.chmod(0o755)
            source = (OVERLAY / 'root/etc/hooks/lmd.d/05-ui-gpu.sh').read_text()
            source = source.replace('/usr/local/bin/extended-config.py', str(tool))
            source = source.replace('/oem/printer_data/config/extended/extended2.cfg', str(config))
            source = source.replace('/sys/class/devfreq/ff320000.gpu/max_freq', str(limit))
            for enabled, device, frequency, expected in (
                    ('true', '/dev/null', '400000000', '1'),
                    ('false', '/dev/null', '400000000', '0'),
                    ('true', str(root / 'missing-device'), '400000000', '0'),
                    ('true', str(config), '400000000', '0'),
                    ('true', '/dev/null', '500000000', '0'),
                    ('true', '/dev/null', None, '0'),
                    (None, '/dev/null', '400000000', '0')):
                with self.subTest(enabled=enabled, device=device, frequency=frequency):
                    config.write_text('[components]\ngui: helixscreen\n' + (f'mali_gpu: {enabled}\n' if enabled else ''))
                    original = config.read_text()
                    if frequency is None:
                        limit.unlink(missing_ok=True)
                    else:
                        limit.write_text(frequency + '\n')
                    script = root / 'policy.sh'
                    script.write_text(source.replace('/dev/mali0', device) + '\nprintf "%s" "$UI_GPU_ENABLED"\n')
                    result = subprocess.run(['sh', str(script), 'start'], capture_output=True, text=True,
                                            env={'PATH': '/usr/bin:/bin', 'UI_GPU_ENABLED': '1'})
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, expected)
                    self.assertEqual(config.read_text(), original)


if __name__ == '__main__':
    unittest.main()
