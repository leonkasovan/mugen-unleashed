#!/usr/bin/env python3
"""Linux host regressions; fake toolchains/devices, no network."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tarfile
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build' / 'port-review'
BUILD.mkdir(parents=True, exist_ok=True)


def function(name):
    text = (ROOT / 'port/arm64/build.sh').read_text()
    start = text.index(name + '() {')
    end = text.index('\n}\n', start) + 3
    return text[start:end]


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o755)


class BuildReview(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=BUILD)
        self.p = Path(self.temp.name)
        self.env = dict(os.environ, TMPDIR=str(self.p))

    def tearDown(self):
        self.temp.cleanup()

    def run_shell(self, body, **extra):
        return subprocess.run(['bash', '-euc', body], cwd=ROOT,
                              env=dict(self.env, **extra), capture_output=True, text=True)

    def installer(self, good_archive=True, managed=True):
        dest = self.p / 'kit'
        cc = dest / 'bin/arm-test-gcc'
        executable(cc, '#!/bin/sh\necho old\n')
        if managed:
            (dest / '.mugen-toolchain-old').write_text('old')
        downloads = self.p / 'downloads'
        downloads.mkdir()
        archive = downloads / 'kit.tar.gz'
        if good_archive:
            stage = self.p / 'package'
            executable(stage / 'bin/arm-test-gcc', '#!/bin/sh\necho new\n')
            with tarfile.open(archive, 'w:gz') as out:
                out.add(stage / 'bin', arcname='bin')
        script = '''
fail() { echo "$*" >&2; exit 1; }
say() { :; }; info() { :; }
fetch() { return 1; }
TOOLCHAIN_VER=new;TOOLCHAIN_FILE=kit.tar.gz;TOOLCHAIN_URL=https://invalid.example/test
TOOLCHAIN_HASH_KIND=sha256;TOOLCHAIN_GLIBC=2.30;OFFLINE=0;CROSS=arm-test-
TOOLCHAIN_DIR="$TEST/kit";CC_BIN="$TOOLCHAIN_DIR/bin/arm-test-gcc"
DOWNLOADS="$TEST/downloads"
TOOLCHAIN_HASH=$(sha256sum "$DOWNLOADS/kit.tar.gz" 2>/dev/null | cut -d' ' -f1) || true
'''
        result = self.run_shell(script + function('hash_of') + function('install_toolchain') + '\ninstall_toolchain', TEST=str(self.p))
        return result, cc

    def test_md5_fallback_does_not_compute_sha1(self):
        (self.p / 'input').write_text('abc')
        script = """
command() {
    if [ "$1" = -v ] && [ "$2" = md5sum ]; then return 1; fi
    builtin command "$@"
}
fail() { exit 1; }
"""
        result = self.run_shell(script + function('hash_of') + '\nhash_of md5 "$TEST/input"', TEST=str(self.p))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '900150983cd24fb0d6963f7d28e17f72')

    def test_failed_download_keeps_installed_compiler(self):
        result, cc = self.installer(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('echo old', cc.read_text())

    def test_verified_managed_replacement(self):
        result, cc = self.installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('echo new', cc.read_text())
        self.assertTrue((cc.parents[1] / '.mugen-toolchain-new').exists())

    def test_unmanaged_toolchain_kept(self):
        result, cc = self.installer(managed=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('echo old', cc.read_text())

    def perf_fixture(self, wait=False):
        work = self.p / 'game'
        (work / 'data').mkdir(parents=True)
        cfg = work / 'data/mugen.cfg'
        original = b'[Video]\r\n  RenderMode = OpenGLScreen\r\n; retained comment\r\n'
        cfg.write_bytes(original)
        executable(work / 'engine', '#!/bin/sh\n' + ('sleep 20\n' if wait else '') +
                   "printf 'Frames      : 60\\nTime elapsed: 1 sec\\n' > mugen.log\n")
        env = dict(self.env, PERF_DIR=str(self.p / 'perf'), PERF_SECONDS='1')
        return work, cfg, original, env

    def test_perf_normal_restores_bytes(self):
        work, cfg, original, env = self.perf_fixture()
        result = subprocess.run(['sh', str(ROOT / 'tools/run_perf.sh'), str(work), 'engine'], env=env, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(cfg.read_bytes(), original)

    def test_perf_signal_restores_bytes(self):
        work, cfg, original, env = self.perf_fixture(True)
        process = subprocess.Popen(['sh', str(ROOT / 'tools/run_perf.sh'), str(work), 'engine'], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            for _ in range(100):
                if cfg.read_bytes() != original:
                    break
                time.sleep(.01)
            self.assertNotEqual(cfg.read_bytes(), original)
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=5)
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(cfg.read_bytes(), original)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

    def test_cross_stamp_distinguishes_toolchains(self):
        ids = []
        extra = self.p / 'print.mk'
        extra.write_text("print-review:\n\t@printf '%s\\n' '$(CONFIG_ID)'\n")
        for version in ['9.2', '15.3']:
            bin_dir = self.p / version
            executable(bin_dir / 'arm-review-gcc', '#!/bin/sh\ncase "$1" in\n-dumpmachine) echo aarch64-linux-gnu;;\n-print-sysroot) echo /compiler/sysroot;;\n-dumpfullversion) echo ' + version + ';;\nesac\n')
            result = subprocess.run(['make', '-s', '-f', 'Makefile', '-f', str(extra), 'print-review',
                                     'CROSS_COMPILE=arm-review-', 'MUGEN_TARGET=linux-arm64',
                                     'XSYSROOT=' + str(self.p / 'sysroot'), 'PKG_CONFIG=true'],
                                    cwd=ROOT, env=dict(self.env, PATH=str(bin_dir) + os.pathsep + self.env['PATH']),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            ids.append(result.stdout)
        self.assertNotEqual(*ids)

    def test_sysroot_pc_names_copied_directories(self):
        root = self.p / 'root'
        lib = root / 'usr/lib/aarch64-linux-gnu'
        lib.mkdir(parents=True)
        inc = root / 'usr/include/SDL2'
        inc.mkdir(parents=True)
        (inc / 'SDL.h').write_text('/* fixture */')
        # Only validate metadata layout, not ARM architecture. A real ELF keeps
        # readelf/SONAME behavior honest without a cross-compiler dependency.
        shutil.copy('/lib/x86_64-linux-gnu/libSDL2-2.0.so.0', lib / 'libSDL2-2.0.so.0')
        dest = self.p / 'sysroot'
        result = subprocess.run(['bash', str(ROOT / 'port/arm64/tools/make_sysroot.sh'), '--root', str(root), '--dest', str(dest)],
                                env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        pc = (dest / 'usr/lib/pkgconfig/sdl2.pc').read_text()
        prefix = Path(next(row[7:] for row in pc.splitlines() if row.startswith('prefix=')))
        self.assertTrue((prefix / 'include/SDL2/SDL.h').is_file())
        self.assertTrue((prefix / 'lib/libSDL2-2.0.so.0').is_file())

    def test_failed_upload_never_extracts_over_live_binary(self):
        bindir = self.p / 'bin'
        executable(bindir / 'sshpass', '#!/bin/sh\nshift 2\nexec "$@"\n')
        executable(bindir / 'ssh', '''#!/bin/bash
cmd=${!#}
if [[ "$cmd" == md5sum* ]]; then echo 'incorrect checksum'; exit 0; fi
cd "$FAKE_DEVICE"
bash -c "$cmd"
''')
        device = self.p / 'device'
        (device / 'game').mkdir(parents=True)
        target = self.p / 'engine'
        target.write_text('new engine')
        live = device / 'game/engine'
        live.write_text('old engine')
        script = '''
fail() { echo "$*" >&2; exit 1; }; say() { :; }; info() { :; }
DEVICE_PASS=test-only;DEVICE=fake;DEVICE_DIR=game;DEVICE_BIN=engine;KEEP_OLD=1
ROOT="$TEST/state";target="$TEST/engine"
'''
        result = self.run_shell(script + function('upload') + '\nupload', TEST=str(self.p), FAKE_DEVICE=str(device),
                                PATH=str(bindir) + os.pathsep + self.env['PATH'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum mismatch', result.stderr)
        self.assertEqual(live.read_text(), 'old engine')


if __name__ == '__main__':
    unittest.main(verbosity=2)
