#!/usr/bin/env python3
"""Exercise the real CMake shader generator without an Android/Vita SDK."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/tmp/model-cmake-test'
# CMake reads this string as code, so a Windows ROOT with backslashes is an
# invalid escape ("Invalid character escape '\P'"). Use forward slashes.
ROOT_CMAKE = ROOT.as_posix()


@unittest.skipUnless(shutil.which('cmake') and shutil.which('cc'), 'CMake/C compiler unavailable')
class ModelBuild(unittest.TestCase):
    def _generate(self, source, env, target):
        result = subprocess.run(['cmake', '-S', str(source), '-B', str(OUT / 'build')],
                                 env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run(['cmake', '--build', str(OUT / 'build'), '--target', target],
                                 env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def _assert_header_matches(self, source, env, shader, variable, header):
        # Binary stdout: the probe is compared byte-for-byte, and a
        # Windows host would otherwise translate every '\n' to CRLF and
        # fail on shaders that are already CRLF in the working tree
        # (core.autocrlf). setmode is a no-op requirement only there.
        (source / 'read.c').write_text('#include <stdio.h>\n'
            f'#include "{header.as_posix()}"\n'
            '#if defined(_WIN32)\n#include <fcntl.h>\n#include <io.h>\n#endif\n'
            'int main(void) {\n'
            '#if defined(_WIN32)\n    _setmode(_fileno(stdout), _O_BINARY);\n#endif\n'
            f'    return fwrite({variable}, 1, sizeof({variable})-1, stdout)'
            f' == sizeof({variable})-1 ? 0 : 1;\n}}\n')
        subprocess.run(['cc', str(source / 'read.c'), '-o', str(OUT / 'read')], env=env, check=True)
        result = subprocess.run([str(OUT / 'read')], env=env, capture_output=True, check=True)
        self.assertEqual(result.stdout, shader.read_bytes(), shader.name)

    def test_shared_shader_target_and_exact_bytes(self):
        source = OUT / 'source'
        source.mkdir(parents=True, exist_ok=True)
        (OUT / 'tmp').mkdir(exist_ok=True)
        env = dict(os.environ, TMPDIR=str(OUT / 'tmp'))
        (source / 'dummy.c').write_text('int model_build_probe;\n')
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(model_probe C)\n'
            'add_library(probe STATIC dummy.c)\n'
            f'include("{ROOT_CMAKE}/port/cmake/Model3D.cmake")\n'
            f'mugen_enable_model3d(probe "{ROOT_CMAKE}")\n'
            f'mugen_enable_sprite_shaders(probe "{ROOT_CMAKE}" gles vita desktop)\n')
        # Each mugen_embed_shaders invocation gets its own custom target, named
        # from its first shader: model.vert and gles/indexed.vert respectively.
        for target in ('probe_shaders_model', 'probe_shaders_indexed'):
            self._generate(source, env, target)

        shaders = [ROOT / 'src/shaders' / n for n in (
            'model.vert', 'model.frag', 'model_basic.vert', 'model_basic.frag',
            'shadow.vert', 'shadow.frag', 'env.vert',
            'env_panorama.frag', 'env_filter.frag')]
        shaders += [ROOT / 'src/shaders' / d / n
                    for d in ('gles', 'vita', 'desktop')
                    for n in ('indexed.vert', 'indexed.frag', 'rgba.vert', 'rgba.frag')]
        for shader in shaders:
            rel = shader.relative_to(ROOT / 'src/shaders')
            variable = 'g_shader_' + rel.as_posix().replace('/', '_').replace('.', '_')
            header = OUT / 'build/model-shaders' / (rel.as_posix() + '.h')
            self.assertTrue(header.exists(), f'missing {header}')
            self._assert_header_matches(source, env, shader, variable, header)


if __name__ == '__main__':
    unittest.main()
