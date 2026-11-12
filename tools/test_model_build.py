#!/usr/bin/env python3
"""Exercise the real CMake shader generator without an Android/Vita SDK."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/tmp/model-cmake-test'


@unittest.skipUnless(shutil.which('cmake') and shutil.which('cc'), 'CMake/C compiler unavailable')
class ModelBuild(unittest.TestCase):
    def test_shared_shader_target_and_exact_bytes(self):
        source = OUT / 'source'
        source.mkdir(parents=True, exist_ok=True)
        (OUT / 'tmp').mkdir(exist_ok=True)
        env = dict(os.environ, TMPDIR=str(OUT / 'tmp'))
        (source / 'dummy.c').write_text('int model_build_probe;\n')
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(model_probe C)\n'
            'add_library(probe STATIC dummy.c)\n'
            f'include("{ROOT}/port/cmake/Model3D.cmake")\n'
            f'mugen_enable_model3d(probe "{ROOT}")\n')
        for args in (['cmake', '-S', str(source), '-B', str(OUT / 'build')],
                     ['cmake', '--build', str(OUT / 'build'), '--target', 'probe_model_shaders']):
            result = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for shader in (ROOT / 'src/shaders').iterdir():
            if shader.suffix not in ('.vert', '.frag'):
                continue
            variable = 'g_shader_' + shader.name.replace('.', '_')
            header = OUT / 'build/model-shaders' / (shader.name + '.h')
            (source / 'read.c').write_text('#include <stdio.h>\n'
                f'#include "{header}"\nint main(void) {{ return '
                f'fwrite({variable}, 1, sizeof({variable})-1, stdout) == sizeof({variable})-1 ? 0 : 1; }}\n')
            subprocess.run(['cc', str(source / 'read.c'), '-o', str(OUT / 'read')], env=env, check=True)
            result = subprocess.run([str(OUT / 'read')], env=env, capture_output=True, check=True)
            self.assertEqual(result.stdout, shader.read_bytes(), shader.name)


if __name__ == '__main__':
    unittest.main()
