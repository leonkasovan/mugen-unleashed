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
        self.assertNotIn(b'-DMUGEN_VITA_SHADER_COMPILER=', result.stdout)
        result = self.arguments('--resolution=480x272', '--shader-compiler', 'C:/My Files/libshacccg.suprx')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_WIDTH=480\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_HEIGHT=272\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_SHADER_COMPILER=C:/My Files/libshacccg.suprx\0', result.stdout)
        for args in [('--resolution',), ('--resolution=0x544',), ('--resolution=0960x544',),
                     ('--renderer=opengl', '--resolution=961x544'), ('--renderer=opengl', '--resolution=960x545'),
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

    def test_supported_output_modes_and_independent_game_size(self):
        for mode in ('960x544', '720x408', '640x368', '480x272'):
            with self.subTest(mode=mode):
                result = self.arguments('--renderer=opengl', '--resolution=' + mode)
                self.assertEqual(result.returncode, 0, result.stderr)
        result = self.arguments('--renderer=opengl', '--game-resolution=640x480',
                                '--output-resolution=960x544')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_WIDTH=640\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_VIDEO_HEIGHT=480\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_OUTPUT_WIDTH=960\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_OUTPUT_HEIGHT=544\0', result.stdout)
        for args in [('--renderer=opengl', '--resolution=640x480'),
                     ('--renderer=opengl', '--output-resolution=800x450'),
                     ('--game-resolution',), ('--output-resolution',),
                     ('--game-resolution=',), ('--output-resolution=',),
                     ('--game-resolution=4097x720',)]:
            with self.subTest(args=args):
                self.assertNotEqual(self.arguments(*args).returncode, 0)

    def cmake(self):
        return os.environ.get('CMAKE') or shutil.which('cmake')

    def patched(self, text, game='960x544', output='960x544', renderer='opengl'):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('    file(READ "${MUGEN_VITA_CFG_SRC}" _cfg)')
        end = source.index('    file(WRITE "${MUGEN_VITA_CFG_OUT}" "${_cfg}")', start)
        end += len('    file(WRITE "${MUGEN_VITA_CFG_OUT}" "${_cfg}")')
        work = ROOT / 'build/tmp/vita-config-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            script = tmp / 'check.cmake'
            helper = (ROOT / 'port/psvita/cmake/Config.cmake').as_posix()
            script.write_text('cmake_minimum_required(VERSION 3.16)\ninclude("' + helper + '")\n' +
                              source[start:end])
            (tmp / 'input.cfg').write_bytes(text.encode())
            gw, gh = game.split('x'); ow, oh = output.split('x')
            args = ['-DMUGEN_VITA_VIDEO_WIDTH=' + gw, '-DMUGEN_VITA_VIDEO_HEIGHT=' + gh,
                    '-DMUGEN_VITA_OUTPUT_WIDTH=' + ow, '-DMUGEN_VITA_OUTPUT_HEIGHT=' + oh,
                    '-DMUGEN_VITA_FULLSCREEN=ON', '-DMUGEN_VITA_RENDERMODE=' + renderer,
                    '-DMUGEN_VITA_PRECACHE=0', '-DMUGEN_VITA_CFG_SRC=' + str(tmp / 'input.cfg'),
                    '-DMUGEN_VITA_CFG_OUT=' + str(tmp / 'output.cfg')]
            result = subprocess.run([self.cmake(), *args, '-P', str(script)], capture_output=True)
            output_file = tmp / 'output.cfg'
            return result, output_file.read_text() if output_file.exists() else ''

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_full_config_patch_and_validation(self):
        template = (ROOT / 'port/psvita/mugen.cfg').read_text()
        cases = [template, template.replace('ShowFPS = 1', 'showfps = 0'),
                 template.replace('ShowFPS = 1', 'ShowFPS=0'),
                 template.replace('ShowFPS = 1', '  ShowFPS = 0'),
                 template.replace('ShowFPS = 1', ';ShowFPS = 0'),
                 template.replace('ShowFPS = 1', 'ShowFPS = 0\nshowfps=0'),
                 template.replace('ShowFPS = 1', ';absent').replace('[Video]', '[Video]\nShowFPS = 0'),
                 template.replace('[Debug]', '[dEbUg]').replace('Debug = 0', 'debug=0'),
                 template.replace('ShowFPS = 1', 'ShowFPS=0').replace('\n', '\r\n'),
                 template.replace('GameWidth = 960', 'gamewidth=320').replace(
                     'GameHeight = 544', ' gameheight=240').replace('[Config]', '[config]'),
                 '[Other]\nWidth = 123\n;semi;colon;and C:\\file\n']
        for text in cases:
            with self.subTest(text=text[:100]):
                result, output = self.patched(text)
                self.assertEqual(result.returncode, 0, result.stderr)
                section = re.search(r'^\[Debug\]\s*$(.*?)(?=^\[|\Z)', output,
                                    re.M | re.S | re.I).group(1)
                self.assertEqual(re.findall(r'^ShowFPS = (\d+)\s*$', section, re.M), ['1'])
                if ';If Debug = 1, this will be ignored.' in text:
                    self.assertIn(';If Debug = 1, this will be ignored.', output)
                if '[Other]' in text:
                    self.assertIn('Width = 123', output)
                    self.assertIn(';semi;colon;and C:\\file', output)
                repeated, again = self.patched(output)
                self.assertEqual(repeated.returncode, 0, repeated.stderr)
                self.assertEqual(output, again)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_config_dimensions_and_template_renderer_validation(self):
        template = (ROOT / 'port/psvita/mugen.cfg').read_text()
        result, output = self.patched(template, game='720x408')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GameWidth = 720', output)
        self.assertIn('GameHeight = 408', output)
        self.assertIn('Width = 960', output)
        self.assertIn('Height = 544', output)
        for mode in ('960x544', '720x408', '640x368', '480x272'):
            result, _ = self.patched(template, game=mode, output=mode)
            self.assertEqual(result.returncode, 0, result.stderr)
        # No explicit renderer still checks the actual config's OpenGL mode.
        gl_template = template.replace('RenderMode = System', 'rendermode=OpenGL')
        for renderer in ('opengl', ''):
            result, _ = self.patched(gl_template, game='640x480', output='640x480', renderer=renderer)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Unsupported Vita OpenGL output', result.stderr)
        for game in ('0x408', '720x0', '4097x408'):
            result, _ = self.patched(template, game=game)
            self.assertNotEqual(result.returncode, 0)
        result, _ = self.patched(template, game='640x480', renderer='system')
        self.assertEqual(result.returncode, 0, result.stderr)
        result, _ = self.patched(template, game='720x408', renderer='openglscreen')
        self.assertNotEqual(result.returncode, 0)
        result, _ = self.patched(template, game='480x272', output='480x272', renderer='openglscreen')
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_compiler_cache_survives_plain_rebuild(self):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('set(MUGEN_VITA_SHADER_COMPILER "" CACHE FILEPATH')
        end = source.index('if(MUGEN_VITA_DATA_DIR)\n  # Each top-level entry', start)
        work = ROOT / 'build/tmp/vita-cache-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            module = tmp / 'compiler with spaces.suprx'
            module.write_bytes(b'SCE\0' + bytes(12))
            helper = (ROOT / 'port/psvita/cmake/Config.cmake').as_posix()
            (tmp / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\n'
                'project(cache_probe NONE)\ninclude("' + helper + '")\n'
                'set(MUGEN_VITA_RENDERMODE opengl)\n' + source[start:end] +
                'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/args.txt" "${_mugen_vpk_args}")\n')
            for args, expected in [(('--shader-compiler', str(module)), True), ((), True),
                                   (('--no-shader-compiler',), False), ((), False)]:
                with self.subTest(args=args):
                    parsed = self.arguments(*args)
                    self.assertEqual(parsed.returncode, 0, parsed.stderr)
                    flags = [s for s in parsed.stdout.decode().split('\0')
                             if s.startswith('-DMUGEN_VITA_SHADER_COMPILER=')]
                    result = subprocess.run([self.cmake(), '-S', str(tmp), '-B', str(tmp / 'out'), *flags],
                                            capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    packed = (tmp / 'out/args.txt').read_text()
                    self.assertEqual('module/libshacccg.suprx' in packed, expected)
                    if not expected:
                        self.assertIn(b'no bundled shader compiler', result.stderr)

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
