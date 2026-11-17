#!/usr/bin/env python3
"""Qualify the unmodified pinned KFM using a --wrap=Round_Tick test executable.
The companion C adapter observes states/damage and sends real SDL key events.
No fixture script is rewritten. Production binaries have no runtime adapter.
"""
import argparse
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--game-dir', type=Path, default=ROOT / 'install')
    p.add_argument('--out', type=Path, default=ROOT / 'build/tmp/zss-kfm-play')
    args = p.parse_args()
    binary, assets, out = args.binary.resolve(), args.game_dir.resolve(), args.out.resolve()
    if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        p.error('--out must be a subdirectory of this repository/build')
    if out.is_relative_to(assets) or assets.is_relative_to(out):
        p.error('--out and --game-dir must not contain each other')
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(['python3', str(ROOT / 'tools/fetch_zss_kfm.py'), '--check', '--game-dir', str(assets)], check=True)
    for mode, opponent in [('human', 'kfm'), ('ai', 'kfm'), ('ai', 'kfm_zss')]:
        name = mode + '-' + opponent
        with tempfile.TemporaryDirectory(prefix=name + '-', dir=out) as temporary:
            game = Path(temporary) / 'game'
            shutil.copytree(assets, game)
            cfg = game / 'data/mugen.cfg'
            text = cfg.read_text()
            for key, value in [('RenderMode', 'System'), ('FullScreen', '0'), ('Precache', '0')]:
                text = re.sub(r'(?mi)^' + key + r'\s*=.*$', key + ' = ' + value, text)
            # The input sequence targets these explicit bindings in the copy.
            def bindings(match):
                section = match.group(0)
                for key, value in [('Jump', 273), ('Crouch', 274), ('Right', 275), ('X', 108), ('A', 44)]:
                    section = re.sub(r'(?mi)^' + key + r'\s*=.*$', key + ' = ' + str(value), section)
                return section
            text = re.sub(r'(?mis)^\[P1 Keys\].*?(?=^\[|\Z)', bindings, text)
            cfg.write_text(text)
            (game / 'mugen.log').unlink(missing_ok=True)
            for image in game.glob('mugen*.png'):
                image.unlink()
            command = [str(binary), '-nosound', '-p1', 'kfm_zss', '-p2', opponent,
                       '-sessionseed', '424242', '-rounds', '1']
            if mode == 'ai':
                command += ['-p1.ai', '1', '-p2.ai', '1']
            env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                       MUGEN_KFM_TEST_MODE=mode, MUGEN_ZSS_TRACE='1')
            with (out / (name + '-stdout.log')).open('w') as output:
                result = subprocess.run(command, cwd=game, env=env, stdout=output,
                                        stderr=subprocess.STDOUT, timeout=120)
            log = (game / 'mugen.log').read_text(errors='replace')
            (out / (name + '.log')).write_text(log)
            if result.returncode or 'Successful program termination.' not in log or 'failed to load' in log or 'ZSS body execution failed' in log:
                raise RuntimeError(name + ': game did not load/run/exit cleanly')
            trace = game / 'kfm-runtime.csv'
            if not trace.exists():
                raise RuntimeError('The executable must link tools/test_zss_kfm_runtime.c with --wrap=Round_Tick')
            rows = list(csv.DictReader(trace.open()))
            shutil.copyfile(trace, out / (name + '.csv'))
            players = [[r for r in rows if r['player'] == str(i)] for i in [1, 2]]
            states = [{int(r['state']) for r in player} for player in players]
            life = [min(int(r['life']) for r in player) for player in players]
            if mode == 'human':
                if not {20, 40, 50, 10, 11, 200, 230}.issubset(states[0]) or life[1] >= 1000:
                    raise RuntimeError(name + ': keyboard movement/attacks/damage missing')
            elif min(life) != 0 or not any(r['round'] == '4' for r in rows):
                raise RuntimeError(name + ': no actual KO and round finish before the frame deadline')
            elif not any(1000 <= state < 4000 for state in states[0] | states[1]):
                raise RuntimeError(name + ': no special/super attack executed')
            for i, image in enumerate(sorted(game.glob('mugen*.png'))):
                shutil.copyfile(image, out / (name + '-' + str(i) + '.png'))
            print('ok:', name, 'life minima', life, 'distinct states', [len(s) for s in states], flush=True)


if __name__ == '__main__':
    main()
