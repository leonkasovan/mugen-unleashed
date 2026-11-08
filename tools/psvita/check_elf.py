#!/usr/bin/env python3
"""Check the linked ARM ELF's metadata gap before vita-elf-create runs."""
import argparse
from pathlib import Path
import struct
import sys


def metadata_gap(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected a little-endian ELF32 file')
    kind, machine = struct.unpack_from('<HH', data, 16)
    if kind != 2 or machine != 40:
        raise ValueError('expected an ARM ET_EXEC input, not a SELF or VELF')
    entry, phoff = struct.unpack_from('<II', data, 24)
    phsize, phnum = struct.unpack_from('<HH', data, 42)
    if phsize < 32 or phnum == 0 or phoff + phsize * phnum > len(data):
        raise ValueError('invalid program-header table')
    loads = []
    for i in range(phnum):
        typ, offset, start, _, filesz, memsz, flags, _ = struct.unpack_from('<8I', data, phoff + i * phsize)
        if typ == 1:
            if filesz > memsz or offset + filesz > len(data) or start + memsz > 0x100000000:
                raise ValueError('invalid LOAD segment range')
            loads.append((start, memsz, flags))
    text = next((s for s in loads if s[2] & 1 and s[0] <= (entry & ~1) < s[0] + s[1]), None)
    if text is None:
        raise ValueError('entry point is not in an executable LOAD segment')
    following = [s[0] for s in loads if s[0] > text[0]]
    if not following:
        raise ValueError('cannot verify SCE headroom without a following LOAD segment')
    return min(following) - (text[0] + text[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--min-gap', type=lambda s: int(s, 0), default=0x10000)
    args = parser.parse_args()
    try:
        gap = metadata_gap(args.elf.read_bytes())
        if gap < args.min_gap:
            raise ValueError('SCE metadata gap %#x is below %#x; check VitaSDK linker support for __sce_headroom'
                             % (gap, args.min_gap))
    except (OSError, ValueError, struct.error) as error:
        print('Vita ELF: ' + str(error), file=sys.stderr)
        return 1
    print('Vita ELF: SCE metadata headroom %#x verified' % gap)
    return 0


if __name__ == '__main__':
    sys.exit(main())
