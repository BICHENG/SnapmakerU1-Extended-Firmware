from contextlib import redirect_stdout
import io
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import egl_smoke


class SuspendAcceptanceTest(unittest.TestCase):
    def probe(self, clients, state):
        output = io.StringIO()
        child = SimpleNamespace(returncode=0, stdout='GPU_SMOKE={"egl_cleanup":true}\n')

        def read(path):
            return '400000000' if path.name == 'max_freq' else state

        with patch.object(egl_smoke.os, 'geteuid', return_value=0), \
             patch.object(Path, 'read_text', read), \
             patch.object(egl_smoke, 'gpu_clients', side_effect=clients), \
             patch.object(egl_smoke.subprocess, 'run', return_value=child), \
             patch.object(egl_smoke.time, 'monotonic', side_effect=(0, 6)), \
             redirect_stdout(output):
            egl_smoke.main()
        return json.loads(output.getvalue())

    def test_isolated_probe_observes_suspend(self):
        self.assertEqual(self.probe([[], []], 'suspended')['suspend_check'], 'passed')

    def test_existing_client_makes_power_check_inconclusive(self):
        result = self.probe([[123], []], 'active')
        self.assertEqual(result['suspend_check'], 'not_isolated')
        self.assertEqual(result['other_gpu_clients'], [123])

    def test_new_client_makes_power_check_inconclusive(self):
        result = self.probe([[], [456]], 'active')
        self.assertEqual(result['suspend_check'], 'not_isolated')
        self.assertEqual(result['other_gpu_clients'], [456])

    def test_isolated_probe_remaining_active_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'did not suspend'):
            self.probe([[], []], 'active')


if __name__ == '__main__':
    unittest.main()
