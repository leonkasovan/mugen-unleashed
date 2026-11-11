#!/usr/bin/env python3
"""Host tests of Vita build arguments and the production ELF layout check."""
import importlib.util
import hashlib
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vita_elf_check', ROOT / 'tools/psvita/check_elf.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def shell_function(source, name):
    """The text of one top-level shell function, braces included."""
    start = source.index(name + '() {')
    end = source.index('\n}\n', start) + len('\n}\n')
    return source[start:end]


class VitaBuildTests(unittest.TestCase):
    def arguments(self, *args):
        source = (ROOT / 'port/psvita/build.sh').read_text()
        start = source.index('BUILD_TYPE=Release')
        end = source.index('# ---------------------------------------------------------------- platform', start)
        parse = source[start:end]
        start = source.index('cmake_args=(')
        end = source.index('cmake "${cmake_args[@]}"', start)
        configure = source[start:end] + 'cmake "${cmake_args[@]}"\n'
        # The script defines HERE and ROOT above the argument parser; the probe
        # has to do the same, or a message that names $HERE is unbound.
        body = ('set -eu\nROOT="$PWD"\nHERE=/project/port/psvita\n'
                'OUT=/project/build/psvita; GENERATOR=Ninja\n') + parse + '''
TOOLCHAIN_FILE_WIN=C:/vitasdk/share/vita.toolchain.cmake
DATA_WIN="$DATA_DIR"
shell_path() { printf '%s' "$1"; }
win_path() { printf '%s' "$1"; }
cmake() { printf '%s\\0' "$@"; }
''' + configure
        return subprocess.run(['bash', '-c', body, 'probe', *args], cwd=ROOT, capture_output=True)

    def test_data_and_titleid_arguments(self):
        result = self.arguments('--data', 'C:/Game Data', '--titleid', 'TEST00001')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = result.stdout.decode().split('\0')
        self.assertIn('-DMUGEN_VITA_DATA_DIR=C:/Game Data', args)
        self.assertIn('-DMUGEN_VITA_TITLEID=TEST00001', args)

    def test_equal_arguments(self):
        result = self.arguments('--titleid=TEST00002', '--data=C:/Other Tree')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_TITLEID=TEST00002\0', result.stdout)
        self.assertIn(b'-DMUGEN_VITA_DATA_DIR=C:/Other Tree\0', result.stdout)

    def test_absent_arguments_apply_port_defaults(self):
        result = self.arguments()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = result.stdout.decode().split('\0')
        # The title id is the only value with a script default.
        self.assertIn('-DMUGEN_VITA_TITLEID=MUGEN0001', args)
        # The renderer, sizes and precache live in the platform config, which is
        # passed as one file and packed verbatim -- never as separate values.
        self.assertIn(b'-DMUGEN_VITA_CFG=', result.stdout)
        self.assertIn(b'/mugen.psvita.cfg\0', result.stdout)
        for retired in (b'-DMUGEN_VITA_RENDERMODE=', b'-DMUGEN_VITA_PRECACHE=',
                        b'-DMUGEN_VITA_VIDEO_WIDTH=', b'-DMUGEN_VITA_OUTPUT_WIDTH='):
            self.assertNotIn(retired, result.stdout)
        # The compiler default is not a flag default: install_vita_libs
        # resolves it from the unpacked release asset (its own test below).
        self.assertNotIn(b'-DMUGEN_VITA_SHADER_COMPILER=', result.stdout)

    def test_bad_arguments_fail(self):
        for args in [('--titleid',), ('--jobs',), ('--data',)]:
            with self.subTest(args=args):
                self.assertNotEqual(self.arguments(*args).returncode, 0)

    def test_retired_arguments_are_rejected(self):
        # Renderer, precache and the sizes moved into port/psvita/mugen.psvita.cfg,
        # which the VPK packs verbatim. The flags are gone rather than silently
        # ignored, so an old invocation fails and names the file.
        for args in [('--renderer',), ('--renderer', 'opengl'), ('--renderer=opengl',),
                     ('--precache',), ('--precache', '0'), ('--precache=0',),
                     ('--resolution',), ('--resolution', '480x272'), ('--resolution=480x272',),
                     ('--game-resolution=720x408',), ('--output-resolution=960x544',)]:
            with self.subTest(args=args):
                result = self.arguments(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'retired', result.stderr)
                self.assertIn(b'mugen.psvita.cfg', result.stderr)

    def test_compiler_input_is_passed_through(self):
        result = self.arguments('--shader-compiler', 'C:/My Files/libshacccg.suprx')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_SHADER_COMPILER=C:/My Files/libshacccg.suprx\0', result.stdout)
        # An explicit clear still reaches CMake, and empties the cached path.
        result = self.arguments('--no-shader-compiler')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'-DMUGEN_VITA_SHADER_COMPILER=\0', result.stdout)
        for args in [('--shader-compiler',), ('--shader-compiler=',)]:
            with self.subTest(args=args):
                self.assertNotEqual(self.arguments(*args).returncode, 0)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_supplied_compiler_input(self):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('set(MUGEN_VITA_SHADER_COMPILER "" CACHE FILEPATH')
        end = source.index('# External compiler use is supported', start)
        work = ROOT / 'build/tmp/vita-build-inputs'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            script = tmp / 'check.cmake'
            script.write_text('set(_vita_renderer opengl)\n'
                              'set(MUGEN_VITA_DATA_DIR "")\n' + source[start:end])
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

    def cmake(self):
        return os.environ.get('CMAKE') or shutil.which('cmake')

    def validated(self, text):
        """Run the platform-config validation block against `text`.

        The block reads MUGEN_VITA_CFG and checks the values in their actual
        groups. Nothing is rewritten any more, so the only output is the exit
        status (and any message), which is exactly what has to be tested.
        """
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('# Validate what it actually says')
        end = source.index('# ------------------------------------------------------------- vitaGL (opt) --', start)
        work = ROOT / 'build/tmp/vita-config-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            (tmp / 'mugen.psvita.cfg').write_bytes(text.encode())
            helper = (ROOT / 'port/psvita/cmake/Config.cmake').as_posix()
            script = tmp / 'check.cmake'
            script.write_text('cmake_minimum_required(VERSION 3.16)\ninclude("' + helper + '")\n' +
                              'set(MUGEN_VITA_CFG "' + (tmp / 'mugen.psvita.cfg').as_posix() + '")\n' +
                              source[start:end])
            return subprocess.run([self.cmake(), '-P', str(script)], capture_output=True)

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_platform_config_is_required_and_validated(self):
        text = (ROOT / 'port/psvita/mugen.psvita.cfg').read_text()
        result = self.validated(text)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'packed verbatim', result.stdout)

        # Case and spacing are not validators; the values in their groups are.
        for variant in (text.replace('RenderMode = OpenGL', 'rendermode=OpenGL'),
                        text.replace('RenderMode = OpenGL', '  RenderMode = opengl  '),
                        text.replace('\n', '\r\n')):
            with self.subTest(variant=variant[:40]):
                self.assertEqual(self.validated(variant).returncode, 0)

        # A missing or unsupported renderer, and missing sizes, are errors.
        for broken, message in [
            (text.replace('RenderMode = OpenGL', 'RenderMode = DirectX'), b'not one of'),
            (text.replace('RenderMode = OpenGL', ';RenderMode = OpenGL'), b'no [Video] RenderMode'),
            (text.replace('GameWidth = 960\n', ''), b'missing a size'),
            (text.replace('Width = 960\n', ''), b'missing a size'),
            (text.replace('GameWidth = 960', 'GameWidth = 0'), b'Invalid Vita'),
        ]:
            with self.subTest(message=message):
                result = self.validated(broken)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

        # OpenGL output must be one of the four panel modes; the game size is free.
        result = self.validated(text.replace('Width = 960', 'Width = 640')
                                    .replace('Height = 544', 'Height = 480'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Unsupported Vita OpenGL output', result.stderr)
        result = self.validated(text.replace('Width = 960', 'Width = 720')
                                    .replace('Height = 544', 'Height = 408')
                                    .replace('GameWidth = 960', 'GameWidth = 720')
                                    .replace('GameHeight = 544', 'GameHeight = 408'))
        self.assertEqual(result.returncode, 0, result.stderr)

        # OpenGLScreen ties the game and display sizes together.
        result = self.validated(text.replace('RenderMode = OpenGL', 'RenderMode = OpenGLScreen')
                                    .replace('GameWidth = 960', 'GameWidth = 480'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'OpenGLScreen uses the game size', result.stderr)

        # FullScreen / FirstRun / Precache warn rather than fail: they are the
        # hardware workarounds, not correctness rules.
        for key, value in [('FullScreen = 1', 'FullScreen = 0'),
                           ('FirstRun = 0', 'FirstRun = 1'),
                           ('Precache = 0', 'Precache = 1')]:
            with self.subTest(key=key):
                result = self.validated(text.replace(key, value))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b'Warning', result.stderr)

    @unittest.skipUnless(shutil.which('unzip') and shutil.which('bash'), 'unzip/bash unavailable')
    def test_default_shader_compiler_is_unpacked_release_asset(self):
        source = (ROOT / 'port/psvita/build.sh').read_text()
        work = ROOT / 'build/tmp/vita-compiler-tests'
        work.mkdir(parents=True, exist_ok=True)
        module = b'SCE\0' + bytes(12)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            downloads = tmp / 'build/downloads'
            downloads.mkdir(parents=True)
            archive = downloads / 'lib-for-vita.zip'
            with zipfile.ZipFile(archive, 'w') as bundle:
                bundle.writestr('libshacccg.suprx', module)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            probe = ('set -eu\n'
                     'DOWNLOADS=' + downloads.as_posix() + '\n'
                     'VITA_LIBS_FILE=lib-for-vita.zip\n'
                     'VITA_LIBS_SHA256=' + digest + '\n'
                     'VITA_LIBS_URL=https://example.invalid/lib-for-vita.zip\n'
                     'OFFLINE=0\nSHADER_COMPILER=""\nSHADER_COMPILER_SET=0\n'
                     'info() { printf \'    %s\\n\' "$*"; }\n'
                     'fail() { printf \'error: %s\\n\' "$*" >&2; exit 1; }\n'
                     'fetch() { fail "fetch must not run with a verified cache"; }\n'
                     + shell_function(source, 'hash_of')
                     + shell_function(source, 'install_vita_libs')
                     + 'install_vita_libs\n'
                     'printf \'RESULT=%s\\n\' "$SHADER_COMPILER"\n')

            def run(offline='0'):
                result = subprocess.run(['bash', '-c', probe.replace('OFFLINE=0', 'OFFLINE=' + offline)],
                                        cwd=tmp, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result.stdout.decode()

            packed = downloads / 'libshacccg.suprx'
            # The default input is the unpacked asset, produced here by unzip.
            self.assertIn('RESULT=' + packed.as_posix(), run())
            self.assertEqual(packed.read_bytes(), module)
            # A second run reuses it instead of re-unpacking.
            self.assertIn('RESULT=' + packed.as_posix(), run())
            # A cache that does not verify, offline: no compiler, no failure.
            archive.write_bytes(b'not the release asset')
            packed.unlink()
            self.assertIn('RESULT=\n', run(offline='1'))
            self.assertFalse(packed.exists())
            # An explicit choice is never overridden by the fetch step.
            explicit = tmp / 'supplied.suprx'
            explicit.write_bytes(module)
            body = (probe.replace('SHADER_COMPILER_SET=0', 'SHADER_COMPILER_SET=1')
                    .replace('SHADER_COMPILER=""', 'SHADER_COMPILER=' + explicit.as_posix()))
            result = subprocess.run(['bash', '-c', body], cwd=tmp, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('RESULT=' + explicit.as_posix(), result.stdout.decode())

    @unittest.skipUnless(os.environ.get('CMAKE') or shutil.which('cmake'), 'CMake unavailable')
    def test_compiler_flag_controls_the_packed_module(self):
        source = (ROOT / 'port/psvita/CMakeLists.txt').read_text()
        start = source.index('set(MUGEN_VITA_SHADER_COMPILER "" CACHE FILEPATH')
        end = source.index('if(MUGEN_VITA_DATA_DIR)', start)
        work = ROOT / 'build/tmp/vita-cache-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            tmp = Path(tmp)
            module = tmp / 'compiler with spaces.suprx'
            module.write_bytes(b'SCE\0' + bytes(12))
            helper = (ROOT / 'port/psvita/cmake/Config.cmake').as_posix()
            (tmp / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\n'
                'project(cache_probe NONE)\ninclude("' + helper + '")\n'
                'set(_vita_renderer opengl)\n'
                'set(MUGEN_VITA_DATA_DIR "")\n'
                'set(MUGEN_VITA_CFG "' + (tmp / 'mugen.psvita.cfg').as_posix() + '")\n'
                'set(VITA_APP_NAME probe)\n'
                'set(MUGEN_VITA_VERSION 01.00)\n'
                'set(MUGEN_VITA_TITLEID TEST00001)\n'
                'set(MUGEN_VITA_ICON_PNG icon.png)\n'
                'set(MUGEN_VITA_BG_PNG bg.png)\n'
                'set(MUGEN_VITA_STARTUP_PNG startup.png)\n'
                'set(MUGEN_VITA_SCE_SYS "${CMAKE_CURRENT_SOURCE_DIR}")\n'
                'set(MUGEN_VITA_SFO sfo)\n'
                'set(MUGEN_VITA_EBOOT eboot)\n'
                + source[start:end] +
                'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/args.txt" "${_mugen_vpk_args}")\n')
            (tmp / 'mugen.psvita.cfg').write_text('[Video]\nRenderMode = OpenGL\n')
            # A plain build's default input only exists after install_vita_libs
            # has unpacked it, which the parser slice above does not run, so the
            # CMake half drives the explicit choices only.
            for args, expected in [(('--shader-compiler', str(module)), True),
                                   (('--no-shader-compiler',), False)]:
                with self.subTest(args=args):
                    parsed = self.arguments(*args)
                    self.assertEqual(parsed.returncode, 0, parsed.stderr)
                    flags = [s for s in parsed.stdout.decode().split('\0')
                             if s.startswith('-DMUGEN_VITA_SHADER_COMPILER=')]
                    target = tmp / ('out-' + str(expected))
                    result = subprocess.run([self.cmake(), '-S', str(tmp), '-B', str(target), *flags],
                                            capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    packed = (target / 'args.txt').read_text()
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
