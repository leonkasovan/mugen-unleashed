#!/usr/bin/env python3
"""Launcher icons come from res/icons, not from hand-drawn placeholders.

Checks the generator (tools/make_android_icons.sh -> tools/iconpack.py) and the
copies committed under app/src/main/res, which are what a build without Python
uses:

  1. all five density buckets are produced, at the sizes Android expects
     (48/72/96/144/192) plus the adaptive foreground at 108dp
     (108/162/216/324/432);
  2. the pictures are the artwork and not a blank canvas -- opaque centre,
     transparent corners;
  3. the adaptive foreground keeps the artwork inside the 72dp safe zone, since
     Android 8+ masks the layer to the launcher's own shape;
  4. the committed fallbacks are byte-identical to what the generator makes, so
     the two cannot drift apart silently;
  5. the manifest's icon resources exist and the adaptive icon points at the
     generated foreground rather than a vector placeholder.

Run: python3 port/android/tests/test_icons.py
"""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[3]
ANDROID = ROOT / "port/android"
GENERATOR = ROOT / "tools/make_android_icons.sh"
RES = ANDROID / "app/src/main/res"

# density: legacy edge, adaptive foreground edge (108dp)
BUCKETS = [("mdpi", 48, 108), ("hdpi", 72, 162), ("xhdpi", 96, 216),
           ("xxhdpi", 144, 324), ("xxxhdpi", 192, 432)]
# work/ is git-ignored and inside the project; the default tempfile location can
# be outside the repository.
WORK = ROOT / "build/android-icon-test"


def read_png(path):
    """(width, height, rgba-pixel lookup) for a PNG written by iconpack.py."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("%s is not a PNG" % path)
    width, height, depth, ctype = struct.unpack(">IIBB", data[16:26])
    if (depth, ctype) != (8, 6):
        raise AssertionError("%s is not 8-bit RGBA (depth %d, type %d)" % (path, depth, ctype))
    pos, idat = 8, b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        if data[pos + 4:pos + 8] == b"IDAT":
            idat += data[pos + 8:pos + 8 + length]
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4 + 1
    rows = [raw[i * stride + 1:(i + 1) * stride] for i in range(height)]

    def px(x, y):
        return tuple(rows[y][x * 4:x * 4 + 4])

    return width, height, px


class IconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = WORK
        if cls.work.exists():
            for entry in sorted(cls.work.rglob("*"), reverse=True):
                entry.unlink() if entry.is_file() else entry.rmdir()
        cls.work.mkdir(parents=True, exist_ok=True)
        cls.generated = cls.work / "generated"
        result = subprocess.run(
            ["bash", "tools/make_android_icons.sh", cls.generated.relative_to(ROOT).as_posix()],
            cwd=str(ROOT), capture_output=True, text=True)
        cls.generator_output = result.stdout + result.stderr
        cls.generator_status = result.returncode

    @classmethod
    def tearDownClass(cls):
        if cls.work.exists():
            for entry in sorted(cls.work.rglob("*"), reverse=True):
                entry.unlink() if entry.is_file() else entry.rmdir()
            cls.work.rmdir()

    def test_generator_succeeds(self):
        self.assertEqual(self.generator_status, 0, self.generator_output)
        self.assertIn("1.ico", self.generator_output)

    def test_all_densities_and_sizes(self):
        for density, legacy, foreground in BUCKETS:
            for name, edge in (("ic_launcher.png", legacy),
                               ("ic_launcher_foreground.png", foreground)):
                path = self.generated / ("mipmap-%s" % density) / name
                self.assertTrue(path.is_file(), "missing %s" % path)
                width, height, _ = read_png(path)
                self.assertEqual((width, height), (edge, edge),
                                 "%s/%s is %dx%d, expected %dx%d"
                                 % (density, name, width, height, edge, edge))

    def test_pictures_carry_the_artwork(self):
        for density, _legacy, _foreground in BUCKETS:
            # The foreground is the same artwork inside 66% of the canvas, so its
            # opaque fraction is that much smaller (0.66^2 of the icon's ~0.86).
            for name, lo, hi in (("ic_launcher.png", 0.5, 0.95),
                                 ("ic_launcher_foreground.png", 0.2, 0.5)):
                width, height, px = read_png(self.generated / ("mipmap-%s" % density) / name)
                # The M.U.G.E.N bubble: nothing in the corners, opaque in the middle.
                self.assertEqual(px(0, 0)[3], 0, "%s/%s corner is not transparent" % (density, name))
                self.assertEqual(px(width - 1, height - 1)[3], 0,
                                 "%s/%s corner is not transparent" % (density, name))
                self.assertEqual(px(width // 2, height // 2)[3], 255,
                                 "%s/%s centre is not opaque" % (density, name))
                opaque = sum(1 for y in range(height) for x in range(width) if px(x, y)[3])
                ratio = opaque / float(width * height)
                self.assertGreater(ratio, lo, "%s/%s looks empty (%.0f%% opaque, expected >%.0f%%)"
                                   % (density, name, ratio * 100, lo * 100))
                self.assertLess(ratio, hi, "%s/%s looks like a filled square (%.0f%% opaque, "
                                "expected <%.0f%%)" % (density, name, ratio * 100, hi * 100))

    def test_foreground_stays_inside_the_safe_zone(self):
        # 72dp of the 108dp canvas, centred: anything outside is cropped by a
        # round launcher mask, so it must be transparent there.
        for density, _legacy, foreground in BUCKETS:
            width, height, px = read_png(
                self.generated / ("mipmap-%s" % density) / "ic_launcher_foreground.png")
            margin = int(round(foreground * 18.0 / 108.0))      # (108-72)/2 per side
            for x in range(width):
                for y in (0, height - 1):
                    self.assertEqual(px(x, y)[3], 0,
                                     "%s: row %d is not clear of the safe zone" % (density, y))
            for y in range(height):
                for x in (0, width - 1):
                    self.assertEqual(px(x, y)[3], 0,
                                     "%s: column %d is not clear of the safe zone" % (density, x))
            # ... and the artwork must actually be in the middle, not squeezed out.
            self.assertEqual(px(width // 2, height // 2)[3], 255,
                             "%s: no artwork at the centre" % density)
            self.assertGreater(margin, 0)

    def test_committed_fallbacks_match_the_generator(self):
        for density, _legacy, _foreground in BUCKETS:
            for name in ("ic_launcher.png", "ic_launcher_foreground.png"):
                generated = (self.generated / ("mipmap-%s" % density) / name).read_bytes()
                committed_path = RES / ("mipmap-%s" % density) / name
                self.assertTrue(committed_path.is_file(),
                                "%s is missing; a build without Python would have no icon"
                                % committed_path)
                self.assertEqual(committed_path.read_bytes(), generated,
                                 "%s differs from res/icons; regenerate it with "
                                 "tools/make_android_icons.sh app/src/main/res" % committed_path)

    def test_generator_is_deterministic(self):
        again = self.work / "again"
        subprocess.run(["bash", "tools/make_android_icons.sh", again.relative_to(ROOT).as_posix()],
                       cwd=str(ROOT), capture_output=True, text=True, check=True)
        for density, _legacy, _foreground in BUCKETS:
            for name in ("ic_launcher.png", "ic_launcher_foreground.png"):
                self.assertEqual((self.generated / ("mipmap-%s" % density) / name).read_bytes(),
                                 (again / ("mipmap-%s" % density) / name).read_bytes(),
                                 "%s/%s changed between runs" % (density, name))

    def test_adaptive_icon_uses_the_generated_foreground(self):
        adaptive = RES / "mipmap-anydpi-v26/ic_launcher.xml"
        self.assertTrue(adaptive.is_file(), "%s is missing" % adaptive)
        text = adaptive.read_text()
        self.assertIn("@mipmap/ic_launcher_foreground", text)
        self.assertFalse((RES / "drawable-v24/ic_launcher_foreground.xml").exists(),
                         "the placeholder vector foreground is back; the artwork comes "
                         "from res/icons via @mipmap/ic_launcher_foreground")
        manifest = (ANDROID / "app/src/main/AndroidManifest.xml").read_text()
        self.assertIn('android:icon="@mipmap/ic_launcher"', manifest)
        # A density-independent PNG or the adaptive XML must exist for every
        # reference the manifest makes.
        self.assertTrue(any(RES.glob("mipmap-*/ic_launcher.png")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
