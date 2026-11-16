#!/usr/bin/env python3
"""Exercise real directory discovery, lossless editing and failed file commits."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
tests = 0


def put(root, path, data):
    p = root / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def character(root, name, definition=None):
    put(root, f"chars/{name}/{definition or name + '.def'}", b"[Info]\nname=Test\n[Files]\nsprite=x.sff\n")


def stage(root, name):
    put(root, f"stages/{name}", b"[Info]\nname=Test\n[StageInfo]\nzoffset=0\n[BGDef]\n")


def fixture(name, data=b"[Characters]\n[ExtraStages]\n[Options]\narcade.maxmatches=6\n"):
    root = out / name
    root.mkdir()
    put(root, "data/select.def", data)
    return root


def run(root, characters=1, stages=1, failure=None, success=True):
    global tests
    args = [str(binary), "data/select.def", str(characters), str(stages)]
    if failure is not None:
        args.append(str(failure))
    result = subprocess.run(args, cwd=root, capture_output=True, text=True)
    if success:
        assert result.returncode == 0, result.stdout + result.stderr
    else:
        assert result.returncode == 1, result.stdout + result.stderr
    assert not (root / "data/select.def.bak.update").exists()
    tests += 1
    return result


try:
    root = fixture("default")
    character(root, "Hero"); stage(root, "Arena.def")
    before = (root / "data/select.def").read_bytes()
    run(root, failure="default")
    assert (root / "data/select.def").read_bytes() == before
    assert not (root / "data/select.def.bak").exists()
    root = fixture("missing-content")
    assert "result:0" in run(root).stdout
    assert not (root / "data/select.def.bak").exists()
    print("PASS: opt-in default and missing content directories leave the roster untouched")

    original = (b"\xef\xbb\xbf; Screenpack roster\r\n [cHaRaCtErS] ; keep\r\n"
                b"KFM, stages\\Existing.def, order=2 ; keep all options\r\n"
                b"randomselect\r\n; commented, random\r\n\r\n"
                b"[ExtraStages]\r\n ./STAGES\\Existing.def ; already here\r\n\r\n"
                b"[Options]\r\narcade.maxmatches=6,1,1\r\n; Last comment")
    root = fixture("format", original)
    character(root, "KFM")
    character(root, "Zulu")
    character(root, "Alpha")
    character(root, "commented")
    character(root, "Mixed", "mixed.DEF")
    character(root, "nested/Hero")  # no recursive discovery
    put(root, "chars/not-a-character/not-a-character.def", b"[Info]\n[Storyboard]\n")
    (root / "chars/empty").mkdir()
    (root / "chars/archive.zip").write_bytes(b"unused")
    stage(root, "Existing.def")
    stage(root, "New.def")
    stage(root, "Upper.DEF")
    stage(root, "nested/Hidden.def")
    put(root, "stages/intro.def", b"[Info]\n[SceneDef]\n")
    result = run(root)
    updated = (root / "data/select.def").read_bytes()
    expected = original.replace(b"[ExtraStages]", b"Alpha, random\r\nMixed/mixed.DEF, random\r\nZulu, random\r\ncommented, random\r\n[ExtraStages]")
    expected = expected.replace(b"[Options]", b"stages/New.def\r\nstages/Upper.DEF\r\n[Options]")
    assert updated == expected, updated
    assert "result:6" in result.stdout
    assert (root / "data/select.def.bak").read_bytes() == original
    if os.name != "nt":
        (root / "data/select.def").chmod(0o640)
    before = (root / "data/select.def").stat().st_mtime_ns
    assert "result:0" in run(root).stdout
    assert (root / "data/select.def").stat().st_mtime_ns == before
    assert (root / "data/select.def.bak").read_bytes() == original
    stage(root, "Later.def")
    run(root)
    assert (root / "data/select.def.bak").read_bytes() == updated
    if os.name != "nt":
        assert (root / "data/select.def").stat().st_mode & 0o777 == 0o640
    print("PASS: exact BOM/CRLF/comments/options preservation, sorted discovery, repeated backup, idempotence")

    for name in ("KFM", "KFM/KFM", "kfm\\kfm.def", "./KFM/./KFM.def"):
        root = fixture("alias-" + str(tests), f"[Characters]\n{name} ; inline\n[ExtraStages]\n[Options]\n".encode())
        character(root, "KFM")
        assert "result:0" in run(root).stdout
        assert not (root / "data/select.def.bak").exists()
    print("PASS: shorthand/explicit character aliases, slash and case normalization, bare entries without commas")

    for text in (b"[Characters]\n", b"[ExtraStages]\n", b"[Options]\nx=1",
                 b"[ExtraStages]\n[Characters]", b"[Characters]\n[Options]\nx=1\n[ExtraStages]",
                 b"[ExtraStages]\n[Options]\nx=1\n[Characters]", b"",
                 b"\xef\xbb\xbf [Characters]\n[ExtraStages]\n"):
        root = fixture("sections-" + str(tests), text)
        character(root, "Hero"); stage(root, "Arena.def")
        run(root)
        data = (root / "data/select.def").read_bytes()
        section = None
        for line in data.removeprefix(b"\xef\xbb\xbf").splitlines():
            line = line.strip()
            if line.startswith(b"["):
                section = line
            if line == b"Hero, random":
                assert section == b"[Characters]", data
            if line == b"stages/Arena.def":
                assert section == b"[ExtraStages]", data
        assert data.count(b"Hero, random") == data.count(b"stages/Arena.def") == 1
        assert "result:0" in run(root).stdout
    print("PASS: missing/reordered sections, EOF sections, empty files, final line without newline")

    for chars, stages in ((0, 0), (0, 1), (1, 0), (1, 1)):
        root = fixture("switches-" + str(tests))
        character(root, "Hero"); stage(root, "Arena.def")
        run(root, chars, stages, "auto")
        data = (root / "data/select.def").read_bytes()
        assert (b"Hero, random" in data) == bool(chars)
        assert (b"stages/Arena.def" in data) == bool(stages)
    print("PASS: independent automatic character/stage switches and disabled no-write path")

    for blocked in ("select.def.update", "select.def.bak", "select.def.bak.update"):
        root = fixture("blocked-" + str(tests))
        character(root, "Hero"); stage(root, "Arena.def")
        before = (root / "data/select.def").read_bytes()
        (root / "data" / blocked).mkdir()
        # A pre-existing staging file/directory must never be deleted.
        args = [str(binary), "data/select.def", "1", "1"]
        result = subprocess.run(args, cwd=root, capture_output=True, text=True)
        assert result.returncode == 1, result.stdout + result.stderr
        assert (root / "data/select.def").read_bytes() == before
        assert (root / "data" / blocked).is_dir()
        for temp in ("select.def.update", "select.def.bak.update"):
            if temp != blocked:
                assert not (root / "data" / temp).exists()
        tests += 1
    print("PASS: blocked staging/backup destinations preserve original and existing files")

    features = subprocess.check_output([str(binary), "--features"], text=True)
    if "faults" in features:
        for failure in ("write", "flush", "close", "rename"):
            root = fixture("failure-" + failure)
            character(root, "Hero"); stage(root, "Arena.def")
            before = (root / "data/select.def").read_bytes()
            run(root, failure=failure, success=False)
            assert (root / "data/select.def").read_bytes() == before
            assert not (root / "data/select.def.update").exists()
        allocation_failures = 0
        for nth in range(1, 100):
            root = fixture("allocation-" + str(nth))
            character(root, "Hero"); stage(root, "Arena.def")
            before = (root / "data/select.def").read_bytes()
            result = subprocess.run([str(binary), "data/select.def", "1", "1", str(nth)], cwd=root,
                                    capture_output=True, text=True)
            assert result.returncode in (0, 1), result.stdout + result.stderr
            assert not (root / "data/select.def.update").exists()
            assert not (root / "data/select.def.bak.update").exists()
            if result.returncode == 0:
                break
            assert (root / "data/select.def").read_bytes() == before
            allocation_failures += 1
        else:
            raise AssertionError("allocation sweep did not finish")
        print(f"PASS: write/flush/close/final-rename failures and {allocation_failures} allocation failure points preserve original")
    print(f"PASS: {tests} file-update cases")
finally:
    shutil.rmtree(out)
