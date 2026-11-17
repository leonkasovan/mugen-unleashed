#!/usr/bin/env python3
"""Opt-in native Linux ZSS smoke tests using copied, complete game assets."""
import argparse
import csv
import io
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
PROBE = "[StateDef -2]\nCtrlSet{value:1}\n"
FUNCTION_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {\n"
    "let x,y=call pair(1.5,7); let total=call sum(5);\n"
    "let min=call identity(-2147483648); let empty=call blank();\n"
    "let fresh=1; if $empty {let fresh=0;} var(58):=1;\n"
    "if $x=7 && $y=1.5 && $total=15 && $min=-2147483648 && $fresh {call finish(65001);}\n"
    "else {call finish(65002);}\nvar(59):=99;\n}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\n"
    "if var(59)!=0 {ChangeState{value:65002}}\ncall mark(1);\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
)
FUNCTIONS = (
    "[Function pair(x,y) a,b]\nlet a=$y;\nlet b=$x;\n"
    "[Function sum(n) r]\nif $n>0 {let prev=call sum($n-1); let r=$n+$prev;} else {let r=0;}\n"
    "[Function identity(x) r]\nlet r=999;\n"
    "[Function blank() r]\n"
    "[Function finish(target)]\nChangeState{value:$target;ctrl:0}\n"
    "[Function mark(value)]\nCtrlSet{value:$value}\n"
)
SOUND_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {\nvar(58):=1;\n"
    "call sound(65000,17,6);\n"
    "if var(57)=987 {ChangeState{value:65001}} else {ChangeState{value:65002}}\n}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\nCtrlSet{value:1}\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[Function sound(group,number,channel)]\n"
    "PlaySnd{value:S $group,$number;channel:$channel;volume:var(57):=987;"
    "volumescale:50;lowpriority:1;freqmul:1.5;loop:1;abspan:120}\n"
)
PALFX_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {var(58):=1; call EXFX(31,47,63,3);\n"
    "if var(57)=425 {ChangeState{value:65001}} else {ChangeState{value:65002}}\n}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\nCtrlSet{value:1}\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[Function EXFX(r,g,b,t)]\n"
    "PalFX{time:var(57):=$t;color:var(57):=var(57)+$r;"
    "add:(var(57):=var(57)+$r),(var(57):=var(57)+$g),(var(57):=var(57)+$b);"
    "mul:(var(57):=var(57)+$t),(var(57):=var(57)+$t),(var(57):=var(57)+$t);"
    "sinadd:(var(57):=var(57)+$r),(var(57):=var(57)+$g),(var(57):=var(57)+$b),(var(57):=var(57)+$t);"
    "invertall:var(57):=var(57)+$t}\n"
    "BGPalFX{time:var(57):=var(57)+$r}\n"
    "AllPalFX{time:var(57):=var(57)+$b}\n"
)
MAP_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {var(58):=1;\n"
    "if map(seed)!=44 || map(ratio)!=1.5 || map(missing)!=0 || !(AI_EXPECT) {ChangeState{value:65002}}\n"
    "call write(6); MapAdd{map:owned;value:0.5}\n"
    "MapSet{map:once;value:var(57):=var(57)+1}\n"
    "if map(OWNED)!=6.5 || map(once)!=1 || var(57)!=1 || (enemynear,map(owned))!=0 {ChangeState{value:65002}}\n"
    "MapSet{map:seed;value:99}\nMapReset{}\n"
    "if map(seed)=44 && map(ratio)=1.5 && map(owned)=0 && map(once)=0 {ChangeState{value:65001}}\n"
    "else {ChangeState{value:65002}}\n}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\nCtrlSet{value:1}\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[Function write(value)]\nMapSet{map:\"OwNeD\";value:$value}\n"
)
ASSERT_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {var(58):=1; ChangeState{value:0;ctrl:1}}\n"
    "[StateDef 0;type:S;movetype:I;physics:N;]\nCtrlSet{value:1}\n"
    "if time=0 {if facing!=-1 {Turn{}} call hold(1);}\n"
    "else if time=1 {if facing!=-1 {ChangeState{value:65002}}}\n"
    "else if time>=2 {if facing=1 {ChangeState{value:65001}} else {ChangeState{value:65002}}}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[Function hold(enabled)]\n"
    "if $enabled {AssertSpecial{flag:noautoturn;flag2:nowalk}}\n"
)
NOCROUCH_PROBE = (
    "[StateDef -2]\n"
    "if var(58)=0 {var(58):=1; ChangeState{value:0;ctrl:1}}\n"
    "if stateNo=10 {\n"
    "if var(57)=2 && command=\"holddown\" {var(57):=3; AssertSpecial{flag:nocrouch} ChangeState{value:0;ctrl:1}}\n"
    "else if var(57)=3 && command=\"holddown\" {ChangeState{value:65001}} else {ChangeState{value:65002}}\n}\n"
    "[StateDef 0;type:S;movetype:I;physics:N;]\nCtrlSet{value:1}\n"
    "if var(57)=0 {AssertSpecial{flag:nocrouch} if command=\"holddown\" {var(57):=1;}}\n"
    "else if var(57)=1 {if command=\"holddown\" {var(57):=2;} else {ChangeState{value:65002}}}\n"
    "else if var(57)=3 {AssertSpecial{flag:nocrouch} ChangeState{value:10}}\n"
    "else {ChangeState{value:65002}}\n"
    "[StateDef 65001;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
    "[StateDef 65002;type:S;movetype:I;physics:N;]\nCtrlSet{value:0}\n"
)


def build_key_injector(out):
    """A Linux-only SDL event source; engine code and game assets are untouched."""
    flags = shlex.split(subprocess.check_output([os.environ.get("PKG_CONFIG", "pkg-config"), "--cflags", "sdl2"], text=True))
    includes = ["src", "third_party/glad-2.1", "third_party/minizip-0.15",
                "third_party/lua-5.1.4/src", "third_party/libresample/include"]
    library = out / "zss-keyinject.so"
    temporary = out / "keyinject-tmp"
    temporary.mkdir(exist_ok=True)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                   "-DSDL_MAIN_HANDLED", "-DMUGEN_MEMPROF_NO_WRAP=1"]
                   + flags + ["-I" + str(ROOT / path) for path in includes]
                   + [str(ROOT / "tools/test_zss_keyinject.c"), "-ldl", "-o", str(library)],
                   env=dict(os.environ, TMPDIR=str(temporary)), check=True)
    return library


def sound_bank():
    """One generated 0.1-second PCM effect; no external/licensed asset."""
    audio = io.BytesIO()
    with wave.open(audio, "wb") as output:
        output.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", 1000 if (i // 100) % 2 else -1000) for i in range(4410)))
    data = audio.getvalue()
    header = struct.pack("<12s4sii", b"ElecbyteSnd\0", b"\0\0\0\1", 1, 512)
    return header.ljust(512, b"\0") + struct.pack("<iiii", 0, len(data), 65000, 17) + data


def check_sound_trace(game, out):
    trace = game / "zss-sound.csv"
    if not trace.is_file():
        raise RuntimeError("sounds: no cache trace; audio/fixture did not initialize")
    shutil.copyfile(trace, out / "sounds-cache.csv")
    with trace.open(newline="") as source:
        rows = list(csv.reader(source))
    requests = {(r[3], r[4]) for r in rows if len(r) == 9 and r[0] == "request"
                and r[5:7] == ["65000", "17"] and int(r[7]) > 0}
    prepared = {(r[2], r[3]) for r in rows if len(r) == 5 and r[0] == "prepare"
                and (r[2], r[4]) in requests}
    started = [r for r in rows if len(r) == 5 and r[0] == "start"
               and (r[2], r[3]) in prepared and int(r[4]) >= 0]
    if not requests or not prepared or not started:
        raise RuntimeError("sounds: generated effect did not decode, prepare and start on SDL_mixer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--game-dir", required=True, help="complete source game tree; never modified")
    parser.add_argument("--out", default="build/tmp/zss-native-loading")
    parser.add_argument("--case", action="append", help="run only the named scenario; repeat to select several")
    args = parser.parse_args()
    binary, assets, out = (Path(p).resolve() for p in (args.binary, args.game_dir, args.out))
    if ROOT not in out.parents or assets == out or assets in out.parents:
        parser.error("output must be inside the repository and outside the source game tree")
    out.mkdir(parents=True, exist_ok=True)
    cases = [
        ("nocrouch-input", "nocrouch", False),
        ("assert-special", "assert", False),
        ("numeric-maps-human", "maps", False),
        ("numeric-maps-ai", "maps", False),
        ("invalid-map-default", "maps", True),
        ("palfx", "palfx", False),
        ("sounds", "sounds", False),
        ("functions", "functions", False),
        ("legacy-cns", "cns", False),
        ("primary", "primary", False),
        ("common-local", "local", False),
        ("common-fallback", "fallback", False),
        ("invalid-primary", "primary", True),
        ("invalid-common", "fallback", True),
    ]
    if args.case:
        unknown = set(args.case) - {case[0] for case in cases}
        if unknown:
            parser.error("unknown scenario: " + ", ".join(sorted(unknown)))
        cases = [case for case in cases if case[0] in args.case]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", MUGEN_ZSS_TRACE="1")
    injector = build_key_injector(out) if any(case[1] == "nocrouch" for case in cases) else None
    for name, route, invalid in cases:
        with tempfile.TemporaryDirectory(prefix=name + "-", dir=out) as temporary:
            game = Path(temporary) / "game"
            shutil.copytree(assets, game)
            (game / "mugen.log").unlink(missing_ok=True)
            if route == "sounds":
                (game / "zss-sound.csv").unlink(missing_ok=True)
            cfg = game / "data/mugen.cfg"
            text = cfg.read_text()
            for key, value in (("RenderMode", "System"), ("FullScreen", "0"), ("Precache", "0")):
                text = re.sub(r"(?mi)^" + key + r"\s*=.*$", key + " = " + value, text)
            if route == "nocrouch":
                def bind_down(section):
                    bound, count = re.subn(r"(?mi)^Crouch\s*=.*$", "Crouch = 274", section[0])
                    if count != 1:
                        raise RuntimeError("nocrouch-input: missing P1 Crouch key binding")
                    return bound
                text, count = re.subn(r"(?ims)^\[P1 Keys\].*?(?=^\[|\Z)", bind_down, text)
                if count != 1:
                    raise RuntimeError("nocrouch-input: missing P1 Keys section")
            if route == "sounds":
                for key, value in (("Sound", "1"), ("Snd.LazyDecode", "1"), ("Snd.Cache.Prewarm", ""),
                                   ("Snd.Cache.Trace", "zss-sound.csv")):
                    pattern = r"(?mi)^" + re.escape(key) + r"\s*=.*$"
                    if re.search(pattern, text):
                        text = re.sub(pattern, key + " = " + value, text)
                    else:
                        text = re.sub(r"(?mi)^\[Sound\]", "[Sound]\n" + key + " = " + value, text, count=1)
            cfg.write_text(text)
            char = game / "chars/probe"
            shutil.copytree(game / "chars/kfm", char)
            definition = (char / "kfm.def").read_text()
            (char / "kfm.def").unlink()
            probe = "[StateDef -2]\nUnknown{}\n" if invalid else PROBE
            if route in ("sounds", "palfx", "maps", "assert", "nocrouch"):
                probe = {"sounds": SOUND_PROBE, "palfx": PALFX_PROBE, "maps": MAP_PROBE,
                         "assert": ASSERT_PROBE, "nocrouch": NOCROUCH_PROBE}[route]
                definition = re.sub(r"(?m)^st\s*=.*$", "st = probe.ZsS\nst9 = kfm.cns", definition)
                if route == "maps":
                    probe = probe.replace("AI_EXPECT", "aiLevelF>0 && aiLevelF=aiLevel" if name.endswith("-ai") else "aiLevelF=0")
                    definition += "\n[Map]\nseed = " + ("var(1)" if invalid else "44") + "\nratio = 1.5\n"
                if route in ("assert", "nocrouch"):
                    # This synthetic fixture owns state 0 so native autoturn
                    # can be observed. Keep the ZSS/CNS collision guard intact;
                    # remove only its duplicate from this copied common file.
                    common, removed = re.subn(
                        r"(?ims)^\[statedef[ \t]+0[ \t]*\].*?(?=^\[statedef[ \t]|\Z)",
                        "", (game / "data/common1.cns").read_text())
                    if removed != 1:
                        raise RuntimeError("assert-special: expected exactly one common state 0")
                    (char / "assert-common.cns").write_text(common)
                    definition = re.sub(r"(?m)^stcommon\s*=.*$", "stcommon = assert-common.cns", definition)
                if route == "sounds":
                    definition = re.sub(r"(?m)^sound\s*=.*$", "sound = probe.snd", definition)
                    (char / "probe.snd").write_bytes(sound_bank())
            elif route == "functions":
                probe = FUNCTION_PROBE
                definition = re.sub(r"(?m)^st\s*=.*$",
                                    "st = probe.ZsS\nst7 = first.zss\nst8 = functions.zss\nst9 = kfm.cns", definition)
                (char / "first.zss").write_text("[Function identity(x) r]\nlet r=$x;\n")
                (char / "functions.zss").write_text(FUNCTIONS)
            elif route == "primary":
                definition = re.sub(r"(?m)^st\s*=.*$", "st = probe.ZsS\nst9 = kfm.cns", definition)
            elif route != "cns":
                definition = re.sub(r"(?m)^stcommon\s*=.*$", "stcommon = probe.ZsS\nst9 = common-base.cns", definition)
                shutil.copyfile(game / "data/common1.cns", char / "common-base.cns")
            target = game / "data/probe.ZsS" if route == "fallback" else char / "probe.ZsS"
            if route != "cns":
                target.write_text(probe)
            else:
                # CNS-only characters must keep ignoring this extension group.
                definition += '\n[Map]\nlegacy = "unparsed extension data"\n'
            (char / "probe.def").write_text(definition)
            command = [str(binary)] + ([] if route == "sounds" else ["-nosound"]) + [
                "-p1", "probe", "-p2", "kfm", "-sessionseed", "424242"]
            if name == "numeric-maps-ai":
                command += ["-p1.ai", "1"]
            process_env = dict(env)
            if route == "nocrouch":
                process_env["LD_PRELOAD"] = str(injector) + (":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
            with (out / (name + "-stdout.log")).open("w") as stdout:
                process = subprocess.Popen(command, cwd=game, env=process_env, stdout=stdout, stderr=subprocess.STDOUT)
                timed_out = False
                try:
                    code = process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    process.terminate()
                    try:
                        code = process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        raise RuntimeError(name + ": engine failed to stop after SIGTERM")
            log = (game / "mugen.log").read_text(errors="replace")
            (out / (name + "-mugen.log")).write_text(log)
            stdout = (out / (name + "-stdout.log")).read_text(errors="replace")
            combined = log + stdout
            if invalid:
                expected = "ZSS character defaults: Invalid numeric [Map] defaults" if route == "maps" else "ZSS probe.ZsS:2:"
                if code != 1 or timed_out or expected not in combined:
                    raise RuntimeError(name + ": expected a located compile failure; inspect " + str(out))
            elif (not timed_out or code not in (0, -signal.SIGTERM)
                  or "Log file successfully closed." not in log
                  or "Character probe.def loaded OK" not in log or "failed to load" in combined
                  or (route != "cns" and "ZSS state -2 body result 0" not in log)
                  or (route == "cns" and "ZSS state" in log)):
                raise RuntimeError(name + ": expected a loaded/running ZSS body; inspect " + str(out))
            if route == "nocrouch" and "zss-keyinject: down" not in stdout:
                raise RuntimeError(name + ": no SDL down event was injected (requires dynamically linked SDL2)")
            if not invalid and route in ("functions", "sounds", "palfx", "maps", "assert", "nocrouch") and (
                "ZSS state -2 body result 5" not in log or "ZSS state 65001 body result 0" not in log
                or "ZSS state 65002 body result" in log
            ):
                raise RuntimeError(name + ": function value/frame/stop checks did not reach success state")
            if route == "sounds":
                check_sound_trace(game, out)
            print("ok: " + name, flush=True)
    print("ok: native ZSS loader/function/controller scenarios (bounded smoke, not full-match qualification)")


if __name__ == "__main__":
    main()
