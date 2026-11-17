#!/usr/bin/env python3
"""Opt-in native Linux loader smoke test using copied, complete game assets."""
import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--game-dir", required=True, help="complete source game tree; never modified")
    parser.add_argument("--out", default="build/tmp/zss-native-loading")
    args = parser.parse_args()
    binary, assets, out = (Path(p).resolve() for p in (args.binary, args.game_dir, args.out))
    if ROOT not in out.parents or assets == out or assets in out.parents:
        parser.error("output must be inside the repository and outside the source game tree")
    out.mkdir(parents=True, exist_ok=True)
    cases = [
        ("functions", "functions", False),
        ("legacy-cns", "cns", False),
        ("primary", "primary", False),
        ("common-local", "local", False),
        ("common-fallback", "fallback", False),
        ("invalid-primary", "primary", True),
        ("invalid-common", "fallback", True),
    ]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", MUGEN_ZSS_TRACE="1")
    for name, route, invalid in cases:
        with tempfile.TemporaryDirectory(prefix=name + "-", dir=out) as temporary:
            game = Path(temporary) / "game"
            shutil.copytree(assets, game)
            (game / "mugen.log").unlink(missing_ok=True)
            cfg = game / "data/mugen.cfg"
            text = cfg.read_text()
            for key, value in (("RenderMode", "System"), ("FullScreen", "0"), ("Precache", "0")):
                text = re.sub(r"(?mi)^" + key + r"\s*=.*$", key + " = " + value, text)
            cfg.write_text(text)
            char = game / "chars/probe"
            shutil.copytree(game / "chars/kfm", char)
            definition = (char / "kfm.def").read_text()
            (char / "kfm.def").unlink()
            probe = "[StateDef -2]\nUnknown{}\n" if invalid else PROBE
            if route == "functions":
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
            (char / "probe.def").write_text(definition)
            command = [str(binary), "-nosound", "-p1", "probe", "-p2", "kfm", "-sessionseed", "424242"]
            with (out / (name + "-stdout.log")).open("w") as stdout:
                process = subprocess.Popen(command, cwd=game, env=env, stdout=stdout, stderr=subprocess.STDOUT)
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
                if code != 1 or timed_out or "ZSS probe.ZsS:2:" not in combined:
                    raise RuntimeError(name + ": expected a located compile failure; inspect " + str(out))
            elif (not timed_out or code not in (0, -signal.SIGTERM)
                  or "Log file successfully closed." not in log
                  or "Character probe.def loaded OK" not in log or "failed to load" in combined
                  or (route != "cns" and "ZSS state -2 body result 0" not in log)
                  or (route == "cns" and "ZSS state" in log)):
                raise RuntimeError(name + ": expected a loaded/running ZSS body; inspect " + str(out))
            if route == "functions" and (
                "ZSS state -2 body result 5" not in log or "ZSS state 65001 body result 0" not in log
                or "ZSS state 65002 body result" in log
            ):
                raise RuntimeError(name + ": function value/frame/stop checks did not reach success state")
            print("ok: " + name, flush=True)
    print("ok: native primary/common ZSS loading (bounded smoke, not full-match qualification)")


if __name__ == "__main__":
    main()
