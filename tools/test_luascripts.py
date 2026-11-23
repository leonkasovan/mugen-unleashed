#!/usr/bin/env python3
"""Exercise embedded Lua restoration without SDL or game assets."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SH = os.environ.get('SH', 'sh')

class LuaScriptsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = ROOT / 'build/tmp'
        base.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='test-luascripts-', dir=base)
        cls.work = Path(cls.temp.name)
        include = cls.work / 'include'
        include.mkdir()
        subprocess.run([SH, str(ROOT / 'tools/embed_resources.sh'),
                        str(ROOT / 'res/lua'), str(include / 'lua_scripts_data.h'),
                        'lua', 'hex', 'lua', 'vector.lua'],
                       cwd=ROOT, check=True)
        harness = cls.work / 'main.c'
        harness.write_text('#include <stdio.h>\n#include "luascripts.h"\n'
                           'int main(void) { printf("%d\\n", LuaScripts_Ensure()); return 0; }\n')
        cls.exe = cls.work / 'test-luascripts'
        env = dict(os.environ, TMPDIR=str(cls.work))
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                       ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Isrc',
                        '-I' + str(include), str(harness), 'src/luascripts.c',
                        '-o', str(cls.exe)],
                       cwd=ROOT, env=env, check=True)
        cls.scripts = {path.name: path.read_bytes()
                       for path in sorted((ROOT / 'res/lua').glob('*.lua'))}

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.case = tempfile.TemporaryDirectory(dir=self.work)
        self.addCleanup(self.case.cleanup)
        self.folder = Path(self.case.name)

    def run_restore(self):
        return int(subprocess.check_output([self.exe], cwd=self.folder, text=True))

    def test_missing_data_extracts_exact_scripts(self):
        self.assertEqual(self.run_restore(), 1)
        for name, content in self.scripts.items():
            self.assertEqual((self.folder / 'data' / name).read_bytes(), content)
        self.assertEqual(self.run_restore(), 0)
        self.assertFalse(list(self.folder.rglob('*.zip')))

    def test_existing_vector_leaves_game_untouched(self):
        data = self.folder / 'data'
        data.mkdir()
        (data / 'vector.lua').write_bytes(b'custom vector')
        self.assertEqual(self.run_restore(), 0)
        self.assertEqual(list(data.iterdir()), [data / 'vector.lua'])
        self.assertEqual((data / 'vector.lua').read_bytes(), b'custom vector')

    def test_missing_vector_preserves_custom_helpers(self):
        data = self.folder / 'data'
        data.mkdir()
        (data / 'init.lua').write_bytes(b'custom init')
        self.assertEqual(self.run_restore(), 1)
        self.assertEqual((data / 'init.lua').read_bytes(), b'custom init')
        self.assertEqual((data / 'vector.lua').read_bytes(), self.scripts['vector.lua'])

    def test_unwritable_destination_reports_failure(self):
        # A file in place of data/ is reliably unwritable even when run as root.
        (self.folder / 'data').write_bytes(b'keep me')
        self.assertEqual(self.run_restore(), -1)
        self.assertEqual((self.folder / 'data').read_bytes(), b'keep me')

if __name__ == '__main__':
    unittest.main()
