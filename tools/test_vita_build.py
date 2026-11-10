#!/usr/bin/env python3
"""Host tests of Vita build arguments and the production ELF layout check."""
import importlib.util
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vita_elf_check', ROOT / 'tools/psvita/check_elf.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class VitaBuildTests(unittest.TestCase):
    def arguments(self, *args):
        source = (ROOT / 'port/psvita/build.sh').read_text()
        start = source.index('BUILD_TYPE=Release')
        end = source.index('# ---------------------------------------------------------------- platform', start)
        parse = source[start:end]
        start = source.index('cmake_args=(')
        end = source.index('cmake "${cmake_args[@]}"', start)
        configure = source[start:end] + 'cmake "${cmake_args[@]}"\n'
        body = 'set -eu\n' + parse + '''
HERE=/project/port/psvita; OUT=/project/build/psvita; GENERATOR=Ninja
TOOLCHAIN_FILE_WIN=C:/vitasdk/share/vita.toolchain.cmake
DATA_WIN="$DATA_DIR"
shell_path() { printf '%s' "$1"; }
win_path() { printf '%s' "$1"; }
cmake() { printf '%s\\0' "$@"; }
''' + configure
        return subprocess.run(['bash', '-c', body, 'probe', *args], cwd=ROOT, capture_output=True)

    def test_explicit_renderer_and_precache(self):
        result = self.arguments('--renderer', 'opengl', '--precache', '0', '--data', 'C:/Game Data')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = result.stdout.decode().split('\0')
        self.assertIn('-DMUGEN_VITA_RENDERMODE=opengl', args)
        self.assertIn('-DMUGEN_VITA_PRECACHE=0', args)
        self.assertIn('-DMUGEN_VITA_DATA_DIR=C:/Game Data', args)

    def test_equal_arguments(self):
        result = self.arguments('--renderer=system', '--precache=1')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_RENDERMODE=system\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_PRECACHE=1\0', result.stdout)

    def test_absent_arguments_preserve_cache(self):
        result = self.arguments()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b'-DMUGEN_VITA_RENDERMODE=', result.stdout)
        self.assertNotIn(b'-DMUGEN_VITA_PRECACHE=', result.stdout)

    def test_bad_arguments_fail(self):
        for args in [('--renderer',), ('--precache',), ('--renderer=wrong',), ('--precache=2',)]:
            with self.subTest(args=args):
                self.assertNotEqual(self.arguments(*args).returncode, 0)

    def test_native_resolution_and_optional_compiler(self):
        result = self.arguments()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_WIDTH=960\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_HEIGHT=544\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_SHADER_COMPILER=\0', result.stdout)
        result = self.arguments('--resolution=480x272', '--shader-compiler', 'C:/My Files/libshacccg.suprx')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_WIDTH=480\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_HEIGHT=272\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_SHADER_COMPILER=C:/My Files/libshacccg.suprx\0', result.stdout)
        for args in [('--resolution',), ('--resolution=0x544',), ('--resolution=0960x544',),
                     ('--resolution=961x544',), ('--resolution=960x545',),
                     ('--shader-compiler',), ('--shader-compiler=',)]:
            with self.subTest(args=args):
                self.assertNotEqual(self.arguments(*args).returncode, 0)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_supplied_compiler_input(self):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('set(MUGEN_VITA_SHADER_COMPILER "" CACHE FILEPATH')
        end = source.index('set(_mugen_vpk_args', start)
        work = ROOT / 'build/tmp/vita-build-inputs'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            script = tmp / 'check.cmake'
            script.write_text(source[start:end])
            module = tmp / 'private module.suprx'
            raw = tmp / 'raw.elf'
            module.write_bytes(b'SCE\0' + bytes(12))
            raw.write_bytes(b'\x7fELF' + bytes(12))
            for path, expected in [('', 0), (str(module), 0), (str(raw), 1),
                                   (str(tmp / 'missing.suprx'), 1), (str(tmp), 1)]:
                with self.subTest(path=path):
                    result = subprocess.run([os.environ.get('CMAKE') or shutil.which('cmake'),
                                             '-DMUGEN_VITA_SHADER_COMPILER=' + path,
                                             '-P', str(script)], capture_output=True)
                    self.assertEqual(result.returncode, expected, result.stderr)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_showfps_normalization(self):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('    # Port fix: normalize an existing ShowFPS key')
        end = source.index('    # --- real joystick button indices', start)
        patch = source[start:end]
        base = '[Debug]\nDebug = 0\n;If Debug = 1, this will be ignored.\n'
        cases = [base, base.replace('\n', '\r\n'), base + 'ShowFPS = 0\n',
                 base + 'ShowFPS = 1\n', base + 'showfps = 0\n',
                 base + ';ShowFPS = 0\n', (ROOT / 'port/psvita/mugen.cfg').read_text()]
        work = ROOT / 'build/tmp/vita-showfps-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            script = tmp / 'check.cmake'
            script.write_text('file(READ "${CMAKE_CURRENT_LIST_DIR}/input.cfg" _cfg)\n' +
                              patch + 'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/output.cfg" "${_cfg}")\n')
            for text in cases:
                with self.subTest(text=text[:80]):
                    (tmp / 'input.cfg').write_bytes(text.encode())
                    subprocess.run([os.environ.get('CMAKE') or shutil.which('cmake'),
                                    '-P', str(script)], check=True, capture_output=True)
                    output = (tmp / 'output.cfg').read_text()
                    values = re.findall(r'^showfps[ \t]*=[ \t]*(\d+)', output, re.M | re.I)
                    self.assertEqual(values, ['1'])
                    self.assertIn(';If Debug = 1, this will be ignored.', output)

    def test_failed_build_preserves_status_and_stops(self):
        source = (ROOT / 'port/psvita/build.sh').read_text()
        start = source.index('build_status=0')
        end = source.index('self="$OUT/', start)
        body = '''set -eu
OUT=/project/build/psvita; JOBS=8
cmake() { return 17; }
info() { printf '%s\\n' "$*"; }
''' + source[start:end] + 'echo SHOULD_NOT_REACH\n'
        result = subprocess.run(['bash', '-c', body], capture_output=True, text=True)
        self.assertEqual(result.returncode, 17)
        self.assertIn('keep /project/build/psvita/mugen', result.stdout)
        self.assertNotIn('SHOULD_NOT_REACH', result.stdout)

    def elf(self, gap):
        data = bytearray(160)
        data[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HH', data, 16, 2, 40)
        struct.pack_into('<II', data, 24, 0x81000001, 52)
        struct.pack_into('<HH', data, 42, 32, 2)
        struct.pack_into('<8I', data, 52, 1, 0, 0x81000000, 0, 0, 0x1000, 5, 0x1000)
        struct.pack_into('<8I', data, 84, 1, 0, 0x81001000 + gap, 0, 0, 0x1000, 6, 0x1000)
        return data

    def test_metadata_gap(self):
        self.assertEqual(check.metadata_gap(self.elf(0x10000)), 0x10000)
        self.assertEqual(check.metadata_gap(self.elf(4)), 4)
        self.assertEqual(check.metadata_gap(self.elf(-4)), -4)

    def test_gap_cli_rejects_tight_layout(self):
        work = ROOT / 'build/tmp/vita-elf-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            elf = Path(tmp) / 'input.elf'
            for gap, expected in [(4, 1), (0x10000, 0)]:
                elf.write_bytes(self.elf(gap))
                result = subprocess.run([sys.executable, str(ROOT / 'tools/psvita/check_elf.py'),
                                         str(elf)], capture_output=True)
                self.assertEqual(result.returncode, expected, result.stderr)

    def test_bgm_abi_selection(self):
        source = (ROOT / 'src/mugen.h').read_text()
        start = source.index('#if !defined(ANDROID_PORT) && (')
        end = source.index('#endif', start) + len('#endif')
        predicate = source[start:end]
        probe = predicate + '\n#ifdef BGM_NATIVE_PTHREAD\nnative\n#else\nlegacy_or_sdl\n#endif\n'
        cases = [
            (['_WIN32', '__SIZEOF_POINTER__=4'], 'legacy_or_sdl'),
            (['_WIN32', '__SIZEOF_POINTER__=8'], 'native'),
            (['__SIZEOF_POINTER__=4', '__vita__'], 'native'),
            (['__SIZEOF_POINTER__=4'], 'native'),
            (['__SIZEOF_POINTER__=8'], 'native'),
            (['ANDROID_PORT', '__SIZEOF_POINTER__=4'], 'legacy_or_sdl'),
            (['ANDROID_PORT', '__SIZEOF_POINTER__=8'], 'legacy_or_sdl'),
            (['_WIN32'], 'legacy_or_sdl'),
        ]
        for defines, expected in cases:
            with self.subTest(defines=defines):
                result = subprocess.run(['gcc', '-E', '-P', '-undef', '-x', 'c', '-'] +
                                        ['-D' + d for d in defines], input=probe,
                                        capture_output=True, text=True, check=True)
                self.assertEqual(result.stdout.strip(), expected)

    def test_malformed_elf(self):
        with self.assertRaises(ValueError):
            check.metadata_gap(b'not ELF')
        with self.assertRaises(ValueError):
            check.metadata_gap(self.elf(0x10000)[:70])
        data = self.elf(0x10000)
        struct.pack_into('<H', data, 16, 0xfe04)
        with self.assertRaises(ValueError):
            check.metadata_gap(data)


if __name__ == '__main__':
    unittest.main()
