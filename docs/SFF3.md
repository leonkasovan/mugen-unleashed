# SFF 3.00 sprite atlases

SFF 3.00 is a mugen-unleashed extension. Run `sffcli --atlas fight.sff` to
write `atlas.fight.sff` in the same directory. The input can be SFF 1.01, 2.00
or 2.01. Existing destinations are refused. Inspect the result with
`sffcli atlas.fight.sff` or extract its individual sprites with
`sffcli --export atlas.fight.sff`.

A DEF referencing `fight.sff` prefers the atlas sibling in the same search
directory or ZIP member directory. The atlas contains everything needed from
the SFF and works if the original is absent. External ACT files still come
from the character DEF. A valid sibling takes precedence regardless of file
dates. Remove or regenerate it after editing the original. An unreadable or
invalid implicit atlas falls back to the named file. An explicit filename
starting with `atlas.` is opened as supplied.

## Wire format

All integers are little-endian. Offsets are relative to the beginning of the
SFF, except the palette payload offset described below. Tables occur in the
order shown, without overlap. The writer zeroes reserved header bytes.

| Header byte | Type | Meaning |
|---|---|---|
| 0 | 12 bytes | `ElecbyteSpr` followed by NUL |
| 12 | u32 | Format version, 0x03000000 |
| 16 | u32 | Source SFF version, 0x01000100, 0x02000000 or 0x02000100 |
| 24 | u32 | Required reader version, 0x03000000 |
| 36, 40 | u32, u32 | Sprite table offset and count |
| 44, 48 | u32, u32 | Palette table offset and count |
| 52, 56 | u32, u32 | Page table offset and count |
| 60 | u32 | Start of payload area |

The header occupies 512 bytes. The **40-byte sprite record** contains group
and number (i32 at 0 and 4), width and height (u16 at 8 and 10), signed axes
(i16 at 12 and 14), logical bit depth (u32 at 16), requested and effective
palette slots (u32 at 20 and 24), image link (u32 at 28), page index (u32 at
32), and the left and top of the image (u16 at 36 and 38). 0xffffffff denotes
no palette or no image link. A link must refer to an earlier sprite and repeat
its resolved rectangle and depth. It can have different axes, IDs or palette.
Slots and duplicate IDs retain archive order. Indexed depths 1 through 8 store
one whole byte per sample.

The **16-byte palette record** uses the v2 layout. Group, number, color count,
and backward link are u16 at 0, 2, 4 and 6. Payload offset relative to the
payload area and stored byte count are u32 at 8 and 12. A zero size selects an
earlier slot. Other records hold 1 to 256 straight RGBA entries. The converter
normalizes v2.00 palette alpha using the same rule as the legacy loader.

The **28-byte page record** consists of seven u32 fields in order: width,
height, storage depth (8, 24 or 32), codec (0 raw or 2 RLE8), absolute payload
offset, stored length, and decoded length. Rows run from top to bottom, tightly
packed, in index, RGB or straight RGBA order. Decoded length must equal
width × height × bytes per sample. RLE8 begins with the little-endian decoded
length and uses the v2 byte-oriented packets across the entire page. Bytes
0x40–0x7f indicate a repeat count in the low six bits followed by the value.
A zero count is invalid. Other bytes are literals.

The converter packs archive-order shelves per storage depth, without trimming
or rotation. Each image has a one-texel extruded border on all sides, corners
included. The rectangle excludes this border. Normal pages fit within
1024 × 1024 and are cropped to their used extent. A larger sprite occupies
a dedicated page with its original samples and border. Unused texels are zero.
Links reuse the same rectangle. RLE8 is used only when smaller than raw.

## Drawing

Eligible consecutive GLES sprites share a draw when their shader, clipping,
masking and blend agree. Both v3 pages and ordinary sprite textures use the
same instanced path for non-stretched quads. Each distinct sprite/palette
texture pair takes one of seven slots. Every instance selects its pair and
carries its own palette effects, so those changes need not split a draw. The
eighth distinct pair starts another draw. Desktop GL still requires a common
page, palette and palette effects.
Draw order follows the game. Desktop GL and GLES upload pages on demand. Legacy effects, unsupported
shaders, VitaGL and the software renderer can obtain contiguous pixel rows for
an individual sprite. Set `MUGEN_SPRITEBATCH=0` to submit one draw at a time.
The binary format has no dependency on a GPU, desktop shader dialect, or page
size supported by a particular driver.

## Verification

Build with `make sffcli sff3-probe`. Run `python tools/test_sff3.py` with
the path to `sffcli` and optionally one or more legacy SFF files. Set
`SFF3_ENGINE_PROBE` to the path to `sff3-test/probe` under the build
directory (add `.exe` on Windows) to exercise the engine loader as well.
`tools/test_gles_smoke.sh` covers native indexed page uploads and batch
submission using an EGL pbuffer.
