#!/usr/bin/env python3
"""Audio script target/exit contracts with mocked compiler; no ABI evidence."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/audio-target-tests"
WORK.mkdir(parents=True, exist_ok=True)


class AudioTargetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=WORK)
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        tools = self.project / "tools"
        tools.mkdir()
        for name in ("test_sdl2audio.sh", "lib_target.sh"):
            shutil.copyfile(ROOT / "tools" / name, tools / name)
        self.cc = self.project / "compiler"
        self.cc.write_text("#!" + sys.executable + "\n" + r'''
import json,os,sys
from pathlib import Path
if sys.argv[1:] == ['-dumpmachine']:
    print('x86_64-w64-mingw32'); sys.exit(0)
args=sys.argv[1:]
Path(os.environ['COMPILER_LOG']).write_text(json.dumps(args))
bits=os.environ.get('FAKE_BITS') or ('64' if '-m64' in args else '32')
out=Path(args[args.index('-o')+1])
out.write_text('#!/bin/sh\necho "abi: '+bits+'-bit build"\nexit '+os.environ.get('FAKE_EXIT','0')+'\n')
out.chmod(0o755)
''')
        self.cc.chmod(0o755)
        self.pkg = self.project / "pkg-config"
        self.pkg.write_text("#!/bin/sh\nexit 0\n")
        self.pkg.chmod(0o755)
        self.env = dict(os.environ, CC=str(self.cc), PKG_CONFIG=str(self.pkg),
                        COMPILER_LOG=str(self.project / "compiler.json"),
                        TMPDIR=str(self.project))
        for key in ("CFLAGS", "LDFLAGS", "SDL_CFLAGS", "SDL_LIBS", "FAKE_BITS", "FAKE_EXIT"):
            self.env.pop(key, None)

    def run_audio(self, target, **extra):
        env = dict(self.env, MUGEN_TEST_TARGET=target, **extra)
        result = subprocess.run(["bash", "tools/test_sdl2audio.sh"], cwd=self.project,
                                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        args = json.loads((self.project / "compiler.json").read_text())
        return result, args

    def test_win32(self):
        result, args = self.run_audio("win32")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("-m32", args)
        self.assertNotIn("-m64", args)
        self.assertIn("-DTEST_EXPECT_BITS=32", args)

    def test_win64(self):
        result, args = self.run_audio("win64")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("-m64", args)
        self.assertNotIn("-m32", args)
        self.assertIn("-DTEST_EXPECT_BITS=64", args)

    def test_wrong_abi_is_rejected(self):
        result, _ = self.run_audio("win64", FAKE_BITS="32")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a 64-bit build", result.stdout)

    def test_skip_is_preserved(self):
        result, _ = self.run_audio("win64", FAKE_EXIT="77")
        self.assertEqual(result.returncode, 77, result.stdout)


if __name__ == "__main__":
    unittest.main()
