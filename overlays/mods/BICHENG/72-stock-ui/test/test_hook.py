import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PreviewGuardTest(unittest.TestCase):
    def test_only_intentional_stop_clears_the_crash_guard(self):
        source=(Path(__file__).parents[1]/'root/etc/hooks/lmd.d/25-stock-ui.sh').read_text()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            guard=root/'guard'
            guard.write_text('1234\n')
            config=root/'config'
            config.write_text('#!/bin/sh\nfor value do :; done\nprintf "%s\\n" "$value"\n')
            config.chmod(0o755)
            hook=root/'hook.sh'
            hook.write_text(source.replace('/tmp/stock-ui.guard',str(guard)).replace('/usr/local/bin/extended-config.py',str(config)))
            def action(value):
                subprocess.run(['sh',str(hook),value],check=True,capture_output=True,
                               env={**os.environ,'UI_GPU_ENABLED':'0'})
            action('start')
            self.assertEqual(guard.read_text(),'1234\n')
            action('stop')
            self.assertFalse(guard.exists())
            action('stop')


if __name__=='__main__':
    unittest.main()
