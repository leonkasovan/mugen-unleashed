#!/usr/bin/env python3
"""Verify the SDL2-only engine sources, build inputs, and executable imports."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ANDROID = 'port/android/app/src/main/jni/src'
VITA = 'port/psvita/src'
SOURCE_DIRS = ('src', ANDROID, VITA, 'tools', 'port/android/tests')
RETIRED_INPUTS = (
    'CMakeLists.txt', 'CMakePresets.json', 'cmake/MugenSdk.cmake',
    'cmake/ThirdParty.cmake', 'tools/msvc_env.bat', 'src/mugen.manifest',
    'src/sdl2compat.h', 'src/sdl2compat.c', 'src/sdlmixercompat.c',
    'third_party/sdk/include/SDL',
)
C_TEXT = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
LEGACY_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"](?:SDL|SDL1(?:\.2)?)/', re.M | re.I)
LEGACY_API = re.compile(
    r'\b(?:SDL_SetVideoMode|SDL_GetVideoSurface|SDL_VideoModeOK|SDL_Flip|SDL_UpdateRects?|'
    r'SDL_WM_\w+|SDL_GL_SwapBuffers|SDL_EnableUNICODE|SDL_EnableKeyRepeat|SDL_GetKeyState|'
    r'SDL_GetWMInfo|SDL_AudioDriverName|SDL_VideoDriverName|SDL_SetAlpha|SDL_SetColors|'
    r'SDL_putenv|SDLKey|SDLMod|zoomSurface|Mix_QuerySpecEx)\b'
)
VERSION_BRANCH = re.compile(
    r'^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif)\b[^\n]*\bMUGEN_SDL2(?:_API)?\b', re.M
)
LEGACY_LIBRARY = re.compile(
    r'(?:libSDL(?:-1\.2|_(?:mixer|ttf|gfx|image|net)(?:-[\d.]+)?)?\.so(?:\.\d+)*|'
    r'SDL(?:-1\.2|_(?:mixer|ttf|gfx|image|net))?\.dll)', re.I
)


def uncomment(match):
    value = match[0]
    return ' ' + '\n' * value.count('\n') if value.startswith('/') else value


def check_sources():
    for name in RETIRED_INPUTS:
        if (ROOT / name).exists():
            yield f'retired build input exists: {name}'

    for directory in SOURCE_DIRS:
        for path in sorted((ROOT / directory).glob('*')):
            if path.suffix not in ('.c', '.h'):
                continue
            source = C_TEXT.sub(uncomment, path.read_text(encoding='utf-8'))
            for rule, reason in (
                (LEGACY_INCLUDE, 'SDL 1.2 include'),
                (LEGACY_API, 'legacy SDL API'),
                (VERSION_BRANCH, 'SDL version-selection branch'),
            ):
                match = rule.search(source)
                if match:
                    line = source.count('\n', 0, match.start()) + 1
                    yield f'{path.relative_to(ROOT)}:{line}: {reason}'

    make = (ROOT / 'Makefile').read_text()
    if re.search(r'^\s*(?:STATIC_PKGS|SDL2_PKGS)\s*:?=.*\b(?:sdl|SDL_gfx|SDL_mixer|SDL_ttf)\b', make, re.M):
        yield 'Makefile requests SDL 1.2 packages'
    workflow = (ROOT / '.github/workflows/build.yml').read_text()
    if re.search(r'msvc_env|cmake --preset|mugen-msvc|sdl1\.exe', workflow):
        yield 'workflow builds/packages a retired target'

    for name in ('port/psvita/CMakeLists.txt', f'{ANDROID}/CMakeLists.txt'):
        if not (ROOT / name).is_file():
            yield f'required platform CMake project missing: {name}'


def check_binary(path):
    with path.open('rb') as file:
        magic = file.read(4)
    if magic == b'\x7fELF':
        program, flag, imports = 'readelf', '-d', r'\(NEEDED\)[^\n]*\[([^]]+)\]'
    elif magic[:2] == b'MZ':
        program, flag, imports = 'objdump', '-p', r'DLL Name:\s+(\S+)'
    else:
        yield f'{path}: expected an ELF or PE binary'
        return
    tool = shutil.which(program)
    if tool is None:
        yield f'{path}: {program} is required to inspect imported libraries'
        return
    result = subprocess.run([tool, flag, str(path)], capture_output=True, text=True)
    if result.returncode:
        yield f'{path}: {program} failed: {result.stderr.strip()}'
        return
    for library in re.findall(imports, result.stdout):
        if LEGACY_LIBRARY.fullmatch(library):
            yield f'{path}: SDL 1.2 dependency {library}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, action='append', default=[],
                        help='ELF or PE binary to inspect (repeatable)')
    args = parser.parse_args()
    errors = list(check_sources())
    for path in args.binary:
        try:
            errors.extend(check_binary(path.resolve()))
        except OSError as exc:
            errors.append(str(exc))
    if errors:
        raise SystemExit('\n'.join(errors))
    suffix = ' and executable imports' if args.binary else ''
    print('ok: SDL2-only source/build contract' + suffix)


if __name__ == '__main__':
    main()
