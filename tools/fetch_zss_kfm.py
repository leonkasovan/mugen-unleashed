#!/usr/bin/env python3
"""Install the pinned upstream KFM ZSS test corpus; keep modified files intact."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from urllib.error import URLError
from urllib.parse import quote
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tools/fixtures/zss_kfm.json"


def inside(base, relative):
    name = PurePosixPath(relative)
    if name.is_absolute() or ".." in name.parts:
        raise ValueError("invalid fixture path: " + relative)
    path = base.joinpath(*name.parts)
    resolved = path.resolve()
    if base != resolved and base not in resolved.parents:
        raise ValueError("fixture path leaves destination: " + relative)
    return path


def valid(data, item):
    return len(data) == item["size"] and hashlib.sha256(data).hexdigest() == item["sha256"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", default="install", help="game tree inside this repository (default: install)")
    parser.add_argument("--offline", action="store_true", help="use verified cached files only")
    parser.add_argument("--check", action="store_true", help="verify installed fixture without downloading or writing")
    args = parser.parse_args()
    try:
        game = Path(args.game_dir)
        game = (game if game.is_absolute() else ROOT / game).resolve()
        if game != ROOT and ROOT not in game.parents:
            raise ValueError("--game-dir must be inside this repository")
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cache = inside(ROOT, manifest["cache"])
        pending = []
        for item in manifest["files"]:
            target = inside(game, item["target"])
            if target.is_symlink():
                raise ValueError("refusing fixture symlink: " + str(target))
            if target.exists():
                if not target.is_file() or not valid(target.read_bytes(), item):
                    raise ValueError("modified fixture file; refusing overwrite: " + str(target))
            elif args.check:
                raise ValueError("fixture file missing: " + str(target))
            else:
                pending.append((target, item))
        if args.check:
            print("ok: {} pinned fixture files verified".format(len(manifest["files"])))
            return
        data_files = []
        for target, item in pending:
            cached = inside(cache, item["cache"])
            data = cached.read_bytes() if cached.is_file() else b""
            if not valid(data, item):
                if args.offline:
                    raise ValueError("verified fixture cache missing: " + str(cached))
                url = "https://raw.githubusercontent.com/{}/{}/{}".format(
                    item["repository"], item["ref"], quote(item["path"], safe="/")
                )
                with urlopen(url, timeout=30) as response:
                    data = response.read(item["size"] + 1)
                if not valid(data, item):
                    raise ValueError("fixture SHA-256/size mismatch: " + item["path"])
                scratch = ROOT / "build/tmp"
                scratch.mkdir(parents=True, exist_ok=True)
                with tempfile.TemporaryDirectory(prefix="zss-kfm-", dir=str(scratch)) as directory:
                    stage = Path(directory) / "download"
                    stage.write_bytes(data)
                    cached.parent.mkdir(parents=True, exist_ok=True)
                    stage.replace(cached)
            data_files.append((target, data, item))
        # All downloads and destination checks finish before installing files.
        # Exclusive creation preserves files another process may have added.
        for target, data, item in data_files:
            target.parent.mkdir(parents=True, exist_ok=True)
            created = False
            try:
                with target.open("xb") as output:
                    created = True
                    output.write(data)
            except FileExistsError:
                if not valid(target.read_bytes(), item):
                    raise ValueError("fixture destination changed: " + str(target))
            except BaseException:
                if created:
                    target.unlink()
                raise
        print("ok: KFM ZSS fixture installed ({} pinned files): {}".format(len(manifest["files"]), game))
    except (OSError, URLError, ValueError) as error:
        parser.exit(1, "error: {}\n".format(error))


if __name__ == "__main__":
    main()
