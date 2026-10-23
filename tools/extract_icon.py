"""Write an icon group of a PE file (by default mugen.exe's group 101) as a .ico file.

Usage: python extract_icon.py EXE OUT.ico [GROUP_ID]

The standalone build embeds the original's window icon this way instead of
keeping a copy of it in the repository.
"""
import struct
import sys

import pefile

RT_ICON = 3
RT_GROUP_ICON = 14


def resources(pe, type_id):
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if entry.id == type_id:
            for res in entry.directory.entries:
                data = res.directory.entries[0].data.struct
                yield res.id, pe.get_data(data.OffsetToData, data.Size)


def main():
    exe, out = sys.argv[1], sys.argv[2]
    group_id = int(sys.argv[3]) if len(sys.argv) > 3 else 101
    pe = pefile.PE(exe, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    icons = dict(resources(pe, RT_ICON))
    group = dict(resources(pe, RT_GROUP_ICON))[group_id]
    # GRPICONDIR: reserved, type, count; GRPICONDIRENTRY: 12 bytes + WORD id (14 bytes)
    _, kind, count = struct.unpack_from("<HHH", group, 0)
    entries = [struct.unpack_from("<BBBBHHIH", group, 6 + 14 * i) for i in range(count)]
    header = struct.pack("<HHH", 0, kind, count)
    offset = 6 + 16 * count
    directory = b""
    images = b""
    for w, h, colors, reserved, planes, bits, _, icon_id in entries:
        image = icons[icon_id]
        directory += struct.pack("<BBBBHHII", w, h, colors, reserved, planes, bits, len(image), offset + len(images))
        images += image
    with open(out, "wb") as f:
        f.write(header + directory + images)


if __name__ == "__main__":
    main()
