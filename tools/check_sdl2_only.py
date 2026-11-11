#!/usr/bin/env python3
"""Reject retired SDL1/MSVC build inputs and SDL1 executable imports."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path)
args = parser.parse_args()
errors = []
for name in ('CMakeLists.txt', 'CMakePresets.json', 'cmake/MugenSdk.cmake',
             'cmake/ThirdParty.cmake', 'tools/msvc_env.bat', 'src/mugen.manifest',
             'src/sdl2compat.h', 'src/sdl2compat.c', 'third_party/sdk/include/SDL'):
    if (ROOT / name).exists():
        errors.append(f'retired build input exists: {name}')
# Historical comments/analysis are allowed. Active source may not call or
# redefine removed SDL functions, nor choose an SDL version at compile time.
legacy = re.compile(r'\b(?:SDL_SetVideoMode|SDL_Flip|SDL_UpdateRect|SDL_WM_\w+|SDL_GL_SwapBuffers|SDL_EnableUNICODE|SDL_EnableKeyRepeat|SDL_GetKeyState|SDL_GetWMInfo|SDL_AudioDriverName|SDL_SetAlpha|SDL_putenv|zoomSurface|Mix_QuerySpecEx)\s*\(')
for path in (ROOT / 'src').glob('*'):
    if path.suffix not in ('.c', '.h'):
        continue
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    if legacy.search(source):
        errors.append(f'legacy SDL API in {path.relative_to(ROOT)}')
    if re.search(r'^\s*#\s*(?:if|ifdef|ifndef|elif).*\bMUGEN_SDL2(?:_API)?\b', source, re.M):
        errors.append(f'SDL version-selection branch in {path.relative_to(ROOT)}')
make = (ROOT / 'Makefile').read_text()
if re.search(r'^\s*STATIC_PKGS\s*:?=.*\b(?:sdl|SDL_gfx|SDL_mixer|SDL_ttf)\b', make, re.M):
    errors.append('Makefile requests SDL1 packages')
workflow = (ROOT / '.github/workflows/build.yml').read_text()
if re.search(r'msvc_env|cmake --preset|mugen-msvc|sdl1\.exe', workflow):
    errors.append('workflow builds/packages a retired target')
for name in ('port/psvita/CMakeLists.txt', 'port/android/app/src/main/jni/src/CMakeLists.txt'):
    if not (ROOT / name).is_file():
        errors.append(f'required platform CMake project missing: {name}')
if args.binary:
    path = args.binary.resolve()
    with path.open('rb') as file:
        magic = file.read(4)
    tool = shutil.which('readelf' if magic == b'\x7fELF' else 'objdump')
    if not tool:
        errors.append('dependency inspection tool unavailable')
    else:
        output = subprocess.check_output([tool, '-d' if magic == b'\x7fELF' else '-p', str(path)], text=True)
        # SDL2 DLLs and ELF libraries have SDL2 in their names, and must not
        # be mistaken for SDL1's differently-versioned addon SONAMEs.
        if re.search(r'(?:libSDL(?:[-_.](?:1[.]2|mixer|ttf|gfx))|\bSDL(?:_mixer|_ttf|_gfx)?\.dll\b)', output, re.I):
            errors.append(f'SDL1 dependency in {path.name}')
if errors:
    raise SystemExit('\n'.join(errors))
print('ok: SDL2-only source/build contract' + (' and executable dependencies' if args.binary else ''))
