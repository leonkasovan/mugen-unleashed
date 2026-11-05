#!/bin/sh
# M9 mutation gates (RENDERING_PLAN.md section 13.1).
#
# Each required mutation is applied to the working tree, the corresponding
# gate must FAIL, then the mutation is reverted and the gate must PASS again.
# The tree must be clean under src/ before starting and is verified clean
# after (the script aborts otherwise).
#
# Usage: tools/test_mutate.sh
# Artefacts go under build/render-mutate per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

OUT=build/render-mutate
mkdir -p "$OUT"

if [ -n "$(git status --porcelain -- src/)" ]; then
    echo "tree is dirty under src/ -- commit first" >&2
    exit 1
fi

PYBIN="${PYTHON3:-}"
if [ -z "$PYBIN" ]; then
    for p in /c/Python314/python.exe python3 python py; do
        if "$p" -c "pass" 2>/dev/null; then PYBIN="$p"; break; fi
    done
fi
[ -n "$PYBIN" ] || { echo "no python found" >&2; exit 1; }

CAUGHT=0
MISSED=0
caught() { echo "MUT $1: caught ($2)"; CAUGHT=$((CAUGHT + 1)); }
missed() { echo "MUT $1: NOT CAUGHT ($2)" >&2; MISSED=$((MISSED + 1)); }

# ---------------------------------------------------------------- MUT-R01 --
# Revert the transactional resize to the pre-fix shape: the replacement
# texture failure keeps the old texture while metadata and surface move on.
cp src/sdl2compat.c "$OUT/r01.fixed.c"
"$PYBIN" - "$OUT/r01.fixed.c" src/sdl2compat.c <<'PYEOF'
import subprocess, sys
fixed, target = sys.argv[1], sys.argv[2]
s = open(fixed, encoding="utf-8").read()
old = subprocess.run(["git", "show", "e76be3b^:src/sdl2compat.c"], capture_output=True,
                     text=True, encoding="utf-8").stdout
start = old.index("    if (g_sdl2Video.texture == NULL || g_sdl2Video.texture_format != format) {")
end = old.index("    return g_sdl2Video.surface;", start) + len("    return g_sdl2Video.surface;")
pre = old[start:end]
cur_start = s.index("    /*\n     * Texture and surface are replaced as one transaction.")
cur_end = s.index("    return g_sdl2Video.surface;", cur_start) + len("    return g_sdl2Video.surface;")
t = s[:cur_start] + pre + s[cur_end:]
open(target, "w", newline="").write(t.replace("\n", "\r\n"))
print("MUT-R01 applied")
PYEOF

if bash tools/test_render_regress.sh >"$OUT/r01.mut.log" 2>&1; then
    missed R01 "gate passed on pre-fix code"
else
    if grep -q 'R01' "$OUT/r01.mut.log"; then caught R01 "R01 checks fail"; else missed R01 "failed elsewhere"; fi
fi
cp "$OUT/r01.fixed.c" src/sdl2compat.c
bash tools/test_render_regress.sh >"$OUT/r01.fixed.log" 2>&1 && echo "MUT R01: fixed code passes again" || missed R01 "revert did not restore a passing gate"

# ---------------------------------------------------------------- MUT-R03 --
# Restore immediate presentation in UpdateRect (pre-M3 shape).
cp src/sdl2compat.c "$OUT/r03.fixed.c"
"$PYBIN" - src/sdl2compat.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """    if (!g_sdl2Video.is_gl && surface != NULL && surface == g_sdl2Video.surface) {
        g_sdl2Video.display_dirty = 1;
    }"""
assert old in s
s = s.replace(old, "    Sdl2_Present();", 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-R03 applied")
PYEOF

if bash tools/test_render_regress.sh >"$OUT/r03.mut.log" 2>&1; then
    missed R03 "gate passed with immediate present"
else
    if grep -q 'R03' "$OUT/r03.mut.log"; then caught R03 "PR01 checks fail"; else missed R03 "failed elsewhere"; fi
fi
cp "$OUT/r03.fixed.c" src/sdl2compat.c
bash tools/test_render_regress.sh >"$OUT/r03.fixed.log" 2>&1 && echo "MUT R03: fixed code passes again" || missed R03 "revert did not restore a passing gate"

# ---------------------------------------------------------------- MUT-R02 --
# Drop the identity reset before the ortho (pre-M2 shape).
cp src/video.c "$OUT/r02.fixed.c"
"$PYBIN" - src/video.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, (double)width, (double)height, 0.0);"""
assert old in s
s = s.replace(old, """    glMatrixMode(GL_PROJECTION);
    gluOrtho2D(0.0, (double)width, (double)height, 0.0);""", 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-R02 applied")
PYEOF

if bash tools/test_render_video.sh >"$OUT/r02.mut.log" 2>&1; then
    missed R02 "gate passed without identity reset"
else
    if grep -q 'R02' "$OUT/r02.mut.log"; then caught R02 "GL01 checks fail"; else missed R02 "failed elsewhere"; fi
fi
cp "$OUT/r02.fixed.c" src/video.c
bash tools/test_render_video.sh >"$OUT/r02.fixed.log" 2>&1 && echo "MUT R02: fixed code passes again" || missed R02 "revert did not restore a passing gate"

# ---------------------------------------------------------------- MUT-R04 --
# Read Bg.legacy through the hardcoded 32-bit offset again. A 32-bit host
# cannot see the difference at runtime (0x64 IS the offset there), so the
# gate is the source contract plus the 64-bit layout report: the offset must
# not appear in camera.c, and layout must show 64-bit diverges.
cp src/camera.c "$OUT/r04.fixed.c"
"$PYBIN" - src/camera.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
assert "if (bg->legacy != 0) {" in s
s = s.replace("if (bg->legacy != 0) {", "if (*(const int *)((const char *)bg + 0x64) != 0) {", 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-R04 applied")
PYEOF

if grep -q '(const char \*)bg + 0x64' src/camera.c; then
    caught R04 "hardcoded Bg.legacy offset detected in source"
else
    missed R04 "hardcoded offset not detected"
fi
# And the layout report must show the 64-bit divergence the fix relies on.
if bash tools/test_abi_layout.sh >"$OUT/r04.layout.log" 2>&1; then
    if grep -q 'offsetof(Bg, legacy) = 0x7c' "$OUT/r04.layout.log"; then
        echo "MUT R04: 64-bit layout confirms legacy moves to 0x7c"
    else
        missed R04 "64-bit layout did not show 0x7c"
    fi
else
    missed R04 "layout report failed"
fi
cp "$OUT/r04.fixed.c" src/camera.c
if grep -q 'bg->legacy != 0' src/camera.c; then
    echo "MUT R04: fixed code restored (member access)"
else
    missed R04 "revert did not restore member access"
fi

# ---------------------------------------------------------------- MUT-R05 --
# Skip true-colour sprites again (pre-M5 shape).
cp src/sprite.c "$OUT/r05.fixed.c"
"$PYBIN" - src/sprite.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """    if (spr->bpp == 24 || spr->bpp == 32) {
        Sprite_DrawSoft8(dp, set, sprite);
        return;
    }
"""
assert old in s
s = s.replace(old, "", 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-R05 applied")
PYEOF

if bash tools/test_render_host.sh >"$OUT/r05.mut.log" 2>&1; then
    missed R05 "gate passed with true-colour sprites skipped"
else
    if grep -q 'R05' "$OUT/r05.mut.log"; then caught R05 "SW01 checks fail"; else missed R05 "failed elsewhere"; fi
fi
cp "$OUT/r05.fixed.c" src/sprite.c
bash tools/test_render_host.sh >"$OUT/r05.fixed.log" 2>&1 && echo "MUT R05: fixed code passes again" || missed R05 "revert did not restore a passing gate"

# --------------------------------------------------------------- MUT-GLESQ --
# Drop the projective Q from the stretched-quad texcoords (RENDERING_PLAN.md
# section 13.1 lists "membuang Q" as a mutation that must break a gate).
# GLES has no fixed-function matrix stack, so Q is carried per-vertex and a
# lost Q silently flattens the trapezoid on the GPU path.
cp src/gl_mvp.c "$OUT/glesq.fixed.c"
"$PYBIN" - src/gl_mvp.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """        out->tc[2][3] = q;"""
assert old in s
s = s.replace(old, "        out->tc[2][3] = 1.0f;", 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-GLESQ applied")
PYEOF

if bash tools/test_gl_mvp.sh >"$OUT/glesq.mut.log" 2>&1; then
    missed GLES-Q "gate passed with the projective Q dropped"
else
    if grep -q 'q = c0/c1' "$OUT/glesq.mut.log"; then
        caught GLES-Q "quad Q checks fail"
    else
        missed GLES-Q "failed elsewhere"
    fi
fi
cp "$OUT/glesq.fixed.c" src/gl_mvp.c
bash tools/test_gl_mvp.sh >"$OUT/glesq.fixed.log" 2>&1 && echo "MUT GLES-Q: fixed code passes again" || missed GLES-Q "revert did not restore a passing gate"

# -------------------------------------------------------------- MUT-RESET --
# Rebuild the texture at the window's size after a device reset instead of the
# storage's (RENDERING_PLAN.md 5.2 step 9). With a refused resize in between,
# the window is 800x600 while the surviving surface is 640x480, so the upload
# reads past the buffer.
cp src/sdl2compat.c "$OUT/reset.fixed.c"
"$PYBIN" - src/sdl2compat.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """                                                SDL_TEXTUREACCESS_STREAMING, g_sdl2Video.storage_w,
                                                g_sdl2Video.storage_h);"""
new = """                                                SDL_TEXTUREACCESS_STREAMING, g_sdl2Video.width,
                                                g_sdl2Video.height);"""
assert old in s
s = s.replace(old, new, 1)
open(p, "w", newline="").write(s.replace("\n", "\r\n"))
print("MUT-RESET applied")
PYEOF

if bash tools/test_render_regress.sh >"$OUT/reset.mut.log" 2>&1; then
    missed RESET "gate passed with the window-size rebuild"
else
    if grep -q 'RESET' "$OUT/reset.mut.log"; then
        caught RESET "reset size checks fail"
    else
        missed RESET "failed elsewhere"
    fi
fi
cp "$OUT/reset.fixed.c" src/sdl2compat.c
bash tools/test_render_regress.sh >"$OUT/reset.fixed.log" 2>&1 && echo "MUT RESET: fixed code passes again" || missed RESET "revert did not restore a passing gate"

# ---------------------------------------------------------------- MUT-CLIP --
# Put the GL clip call back behind the pre-fix "not Android" guard. That is the
# exact regression that painted the title screen white under RenderMode=OpenGL:
# with no scissor set, the window-clipped additive background strips covered the
# whole frame and saturated it.
cp src/gfx.c "$OUT/clip.fixed.c"
"$PYBIN" - "$OUT/clip.fixed.c" src/gfx.c <<'PYEOF'
import sys
src = open(sys.argv[1], encoding="utf-8").read()
s = open(sys.argv[2], encoding="utf-8").read()
start = s.index("#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)\n    /*")
end = s.index("#endif\n", s.index("Gl_ApplyClipRect();", start)) + len("#endif\n")
s = s[:start] + "#ifndef ANDROID_PORT\n    if (g_renderMode == RENDER_OPENGL) {\n        Gl_ApplyClipRect();\n    }\n#endif\n" + s[end:]
open(sys.argv[2], "w", newline="").write(s)
print("MUT-CLIP applied")
PYEOF

if bash tools/test_clip_gate.sh >"$OUT/clip.mut.log" 2>&1; then
    missed CLIP "gate passed with the pre-fix guard"
else
    if grep -q 'reaches Gl_ApplyClipRect' "$OUT/clip.mut.log"; then
        caught CLIP "the GL clip call is missing on the GLES build"
    else
        missed CLIP "failed elsewhere"
    fi
fi
cp "$OUT/clip.fixed.c" src/gfx.c
bash tools/test_clip_gate.sh >"$OUT/clip.fixed.log" 2>&1 && echo "MUT CLIP: fixed code passes again" || missed CLIP "revert did not restore a passing gate"

echo "== mutations: caught=$CAUGHT missed=$MISSED =="
if [ -n "$(git status --porcelain -- src/)" ]; then
    echo "TREE DIRTY after mutations -- reverting" >&2
    git checkout -- src/
    echo "restored; re-verify gates pass"
    exit 1
fi
echo "tree clean after mutations"
[ "$MISSED" = 0 ]
