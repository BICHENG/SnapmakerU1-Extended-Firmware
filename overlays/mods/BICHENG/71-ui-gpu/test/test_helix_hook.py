import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).parents[5]
HOOK = REPO / 'overlays/firmware-extended/69-app-helixscreen/root/etc/hooks/lmd.d/30-helixscreen.sh'
CONFIG_TOOL = REPO / 'overlays/firmware-extended/02-firmware-config/root/usr/local/bin/extended-config.py'


class HelixGpuHookTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.commands = self.root / 'commands'
        self.commands.mkdir()
        self.trace = self.root / 'trace'
        self.config = self.root / 'extended2.cfg'
        self.config.write_text('[components]\ngui: helixscreen\nmali_gpu: false\n')
        self.app = self.root / 'apps/helixscreen'
        self.app.mkdir(parents=True)
        self.latest = self.app / 'latest'
        self.install('1.0.7')
        self.patched = self.root / 'helix-screen-gpu'
        self.patched.write_bytes(b'\x7fELF')
        self.patched.chmod(0o755)
        self.version = self.root / 'helix-screen-gpu.version'
        self.version.write_text('1.0.7\n')
        self.gui = self.root / 'gui'
        self.gui.touch()
        self.mounts = self.root / 'mounts'
        self.mounts.touch()
        self.wpa = self.root / 'system-wpa.conf'
        self.wpa.write_text('saved network\n')
        self.saved_wpa = self.root / 'gui-state/wpa_supplicant.conf'
        self.config_dir = self.root / 'helix-config'
        config_tool = self.commands / 'extended-config.py'
        shutil.copyfile(CONFIG_TOOL, config_tool)
        config_tool.chmod(0o755)
        for name in ('mount', 'umount', 'logger', 'chown', 'killall', 'sleep', 'wpa_supplicant'):
            command = self.commands / name
            command.write_text('#!/bin/sh\n{ printf "%s" "${0##*/}"; printf "\\t%s" "$@"; printf "\\n"; } >> "$TRACE"\n')
            command.chmod(0o755)
        source = HOOK.read_text()
        for original, replacement in (
                ('/oem/apps/helixscreen', self.app),
                ('/oem/printer_data/config/extended/extended2.cfg', self.config),
                ('/oem/printer_data/config/extended/helixscreen', self.config_dir),
                ('/oem/printer_data/gui/wpa_supplicant.conf', self.saved_wpa),
                ('/etc/wpa_supplicant.conf', self.wpa),
                ('/usr/local/bin/extended-config.py', config_tool),
                ('/usr/local/libexec/helix-screen-gpu.version', self.version),
                ('/usr/local/libexec/helix-screen-gpu', self.patched),
                ('/usr/bin/gui', self.gui),
                ('/proc/mounts', self.mounts)):
            source = source.replace(original, str(replacement))
        self.script = self.root / 'hook.sh'
        self.script.write_text(source + '\n' + '\n'.join(
            f'printf "{key}=%s\\n" "${{{key}-}}"' for key in (
                'UI_GPU_ENABLED', 'HELIX_DATA_DIR', 'HELIX_CONFIG_DIR', 'HELIX_SUPERVISED',
                'HELIX_DRM_DEVICE', 'HELIX_REMOTE_SCREEN_FB0', 'HELIX_CACHE_DIR')) + '\n')

    def install(self, version, absolute=False):
        directory = self.app / ('helixscreen-' + version)
        (directory / 'bin').mkdir(parents=True, exist_ok=True)
        binary = directory / 'bin/helix-screen'
        binary.write_bytes(b'\x7fELF')
        binary.chmod(0o755)
        self.latest.unlink(missing_ok=True)
        self.latest.symlink_to(directory if absolute else directory.name)

    def start(self, enabled='0', action='start'):
        self.trace.unlink(missing_ok=True)
        config_before = self.config.read_text()
        result = subprocess.run(['sh', str(self.script), action], capture_output=True, text=True,
                                env={**os.environ, 'PATH': str(self.commands) + ':/usr/bin:/bin',
                                     'TRACE': str(self.trace), 'UI_GPU_ENABLED': enabled})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.config.read_text(), config_before)
        calls = [line.split('\t') for line in self.trace.read_text().splitlines()] if self.trace.exists() else []
        environment = dict(line.split('=', 1) for line in result.stdout.splitlines())
        return calls, environment

    def assert_bound(self, calls, binary):
        self.assertEqual([call for call in calls if call[0] == 'mount'],
                         [['mount', '-o', 'ro', '--bind', str(binary), str(self.gui)]])

    def test_matching_assets_use_same_patched_binary_with_gpu_on_or_off(self):
        for enabled in ('0', '1'):
            with self.subTest(enabled=enabled):
                calls, environment = self.start(enabled)
                self.assert_bound(calls, self.patched)
                self.assertEqual(environment, {
                    'UI_GPU_ENABLED': enabled, 'HELIX_DATA_DIR': str(self.latest),
                    'HELIX_CONFIG_DIR': str(self.config_dir), 'HELIX_SUPERVISED': '1',
                    'HELIX_DRM_DEVICE': '/dev/dri/card0', 'HELIX_REMOTE_SCREEN_FB0': '/dev/fb0',
                    'HELIX_CACHE_DIR': '/userdata/helixscreen/cache'})
                self.assertEqual([c for c in calls if c[0] == 'killall'], [['killall', 'wpa_supplicant']])
                self.assertEqual([c for c in calls if c[0] == 'wpa_supplicant'],
                                 [['wpa_supplicant', '-B', '-i', 'wlan0', '-c', str(self.saved_wpa)]])
                self.assertEqual(self.saved_wpa.read_text(), 'saved network\n')

    def test_absolute_latest_symlink_is_resolved(self):
        self.install('1.0.7', absolute=True)
        calls, _ = self.start('1')
        self.assert_bound(calls, self.patched)

    def test_installed_version_mismatch_keeps_official_binary(self):
        for version in ('1.0.6', '1.0.8'):
            with self.subTest(version=version):
                self.install(version)
                calls, _ = self.start('1')
                self.assert_bound(calls, self.latest / 'bin/helix-screen')

    def test_missing_or_mismatched_patched_metadata_keeps_official_binary(self):
        for version in ('', '1.0.8', None):
            with self.subTest(version=version):
                if version is None:
                    self.version.unlink()
                else:
                    self.version.write_text(version + '\n')
                calls, _ = self.start('1')
                self.assert_bound(calls, self.latest / 'bin/helix-screen')

    def test_nonexecutable_patched_binary_keeps_official_binary(self):
        self.patched.chmod(0o644)
        calls, _ = self.start('1')
        self.assert_bound(calls, self.latest / 'bin/helix-screen')

    def test_same_version_directory_outside_package_root_is_not_trusted(self):
        external = self.root / 'external/helixscreen-1.0.7'
        shutil.copytree(self.latest.resolve(), external)
        self.latest.unlink()
        self.latest.symlink_to(external)
        calls, _ = self.start('1')
        self.assert_bound(calls, self.latest / 'bin/helix-screen')

    def test_existing_bind_is_not_stacked_and_wifi_config_is_preserved(self):
        self.mounts.write_text(f'/old/helix-screen {self.gui} none ro 0 0\n')
        self.saved_wpa.parent.mkdir()
        self.saved_wpa.write_text('user network\n')
        calls, _ = self.start('1')
        self.assertEqual([c for c in calls if c[0] in ('mount', 'umount')], [])
        self.assertEqual(self.saved_wpa.read_text(), 'user network\n')

    def test_snapmaker_selection_restores_stock_without_network_changes(self):
        self.config.write_text('[components]\ngui: snapmaker\nmali_gpu: true\n')
        self.mounts.write_text(f'/old/helix-screen {self.gui} none ro 0 0\n')
        calls, environment = self.start('1')
        self.assertEqual([c for c in calls if c[0] != 'logger'], [['umount', str(self.gui)]])
        self.assertEqual(environment['HELIX_DATA_DIR'], '')

    def test_missing_assets_do_not_start_patched_binary_or_network(self):
        (self.latest / 'bin/helix-screen').unlink()
        calls, environment = self.start('1')
        self.assertEqual(calls, [])
        self.assertEqual(environment['HELIX_DATA_DIR'], '')

    def test_stop_has_no_side_effects(self):
        calls, _ = self.start(action='stop')
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
