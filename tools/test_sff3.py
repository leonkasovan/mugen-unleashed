"""Exercise the standalone converter with deterministic legacy archives.

Usage: python tools/test_sff3.py PATH/TO/sffcli [legacy.sff ...]
All fixtures are created and removed under build/tmp.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


def archive(path):
    """Two indexed palettes, a link with an override, RGB, RGBA and a wide page."""
    colors = bytes(v for i in range(256) for v in (i, 255-i, i//2, 255 if i else 0))
    alt = bytes(v for i in range(256) for v in (i//2, i, 255-i, 255 if i else 0))
    images = [bytes([0, 1, 2, 1, 0, 2]), b'', bytes(range(18)), bytes(range(24)),
              bytes([17])*1031, bytes([2, 0, 1, 1, 2, 0])]
    sprites = [(9000,0,3,2,8,0), (9000,1,3,2,8,1), (4,2,3,2,24,65535),
               (4,3,3,2,32,65535), (4,4,1031,1,8,2), (9000,0,3,2,8,0)]
    palettes = [(1,1,256,0,0,1024), (1,2,256,0,1024,1024), (1,3,256,0,0,0)]
    head = bytearray(512)
    head[:12] = b'ElecbyteSpr\0'
    struct.pack_into('<I', head, 12, 0x02000100)
    struct.pack_into('<I', head, 24, 0x02000100)
    palat = 512 + len(sprites)*28
    ldata = palat + len(palettes)*16
    data = bytearray(colors + alt)
    nodes = bytearray()
    for i, (group,num,w,h,depth,pal) in enumerate(sprites):
        img = images[i]
        nodes.extend(struct.pack('<HHHHhhHBBIIHH',group,num,w,h,-i,i,0,0,depth,len(data),len(img),pal,0))
        data.extend(img)
    struct.pack_into('<8I', head,36,512,len(sprites),palat,len(palettes),ldata,len(data),ldata+len(data),0)
    path.write_bytes(head + nodes + b''.join(struct.pack('<HHHHII', *p) for p in palettes) + data)


def run(cli, *args, success=True):
    p = subprocess.run([cli, *args], capture_output=True, timeout=60)
    assert (p.returncode == 0) == success, (args, p.stderr)
    return p.stdout


def compare(cli, source):
    atlas = source.with_name('atlas.' + source.name)
    run(cli, '--atlas', source)
    saved = atlas.read_bytes()
    run(cli, '--atlas', source, success=False)
    assert atlas.read_bytes() == saved
    run(cli, '--atlas', atlas, success=False)
    assert not atlas.with_name('atlas.' + atlas.name).exists()
    run(cli, atlas)
    for p in (source, atlas):
        run(cli, '--export', p)
    def files(p):
        return {f.name: f.read_bytes() for f in p.with_suffix('').glob('*.png')}
    actual = files(atlas)
    assert actual and actual == files(source)
    print(source.name, len(actual), 'identical PNGs')
    atlas.unlink()
    run(cli, '--atlas', source)
    assert atlas.read_bytes() == saved
    return atlas


def main():
    cli = Path(sys.argv[1]).resolve()
    scratch = Path(__file__).resolve().parents[1] / 'build' / 'tmp'
    scratch.mkdir(exist_ok=True, parents=True)
    with tempfile.TemporaryDirectory(dir=scratch) as d:
        d = Path(d)
        source = d / 'fixture.sff'
        archive(source)
        atlas = compare(cli, source)
        blob = atlas.read_bytes()
        st, n, pt, npal, at, pages, data = struct.unpack_from('<7I', blob, 36)
        assert n == 6 and npal == 3 and pages == 4
        assert struct.unpack_from('<III', blob, st+40+20) == (1,1,0)
        assert struct.unpack_from('<I',blob,st+4*40+32)[0] != struct.unpack_from('<I',blob,st+32)[0]
        # Bad link, out-of-page rectangle, invalid codec, overflowing payload and truncation.
        for offset, value in [(st+40+28,1), (st+36,0xffffffff), (at+12,17), (at+16,0xffffffff)]:
            bad = bytearray(blob)
            struct.pack_into('<I', bad, offset, value)
            path = d/'bad.sff'
            path.write_bytes(bad)
            run(cli, path, success=False)
        path.write_bytes(blob[:-1])
        run(cli, '--export', path, success=False)
        assert not path.with_suffix('').exists()
        v200 = d / 'legacy200.sff'
        legacy_bytes = bytearray(source.read_bytes())
        for offset in (12, 24):
            struct.pack_into('<I', legacy_bytes, offset, 0x02000000)
        v200.write_bytes(legacy_bytes)
        converted200 = compare(cli, v200)
        # Invoke the optional production-loader probe on loose and ZIP members.
        engine = os.environ.get('SFF3_ENGINE_PROBE')
        if engine:
            run(engine, v200, converted200)
            for case,legacy,side in [('both',True,blob),('only',False,blob),('fallback',True,b'bad')]:
                sub = d/case
                sub.mkdir()
                if legacy:
                    (sub/source.name).write_bytes(source.read_bytes())
                (sub/atlas.name).write_bytes(side)
                zpath = sub/'char.zip'
                with zipfile.ZipFile(zpath,'w',zipfile.ZIP_DEFLATED) as z:
                    z.writestr('char.def', '[Files]\n')
                    for f in sub.glob('*.sff'):
                        z.write(f, 'assets/'+f.name)
                run(engine, source, sub/source.name, zpath, case)
        for i, name in enumerate(sys.argv[2:]):
            path = d / ('sample%d.sff' % i)
            path.write_bytes(Path(name).read_bytes())
            converted = compare(cli, path)
            if engine:
                run(engine, path, converted)


if __name__ == '__main__':
    main()
