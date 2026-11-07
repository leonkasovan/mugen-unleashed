#!/bin/sh
# Run each renderer with a temporary config and restore the exact original
# bytes on success, failure or interruption. Game assets are never rewritten.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work=$(CDPATH= cd -- "${1:?installation directory}" && pwd)
bin=${2:?installed binary}; shift 2
out=${PERF_DIR:-"$root/build/perf"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
backup="$out/mugen.cfg.orig"
cp "$work/data/mugen.cfg" "$backup"
restore() { cp "$backup" "$work/data/mugen.cfg"; }
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
for mode in OpenGL System; do
    echo "  PERF  $mode"
    # Accept indentation/case and require exactly one active RenderMode row.
    awk -v mode="$mode" '
      tolower($0) ~ /^[ \t]*rendermode[ \t]*=/ { print "RenderMode = " mode; n++; next }
      {print}
      END {if(n != 1) exit 1}
    ' "$backup" > "$out/mugen.cfg.$mode"
    cp "$out/mugen.cfg.$mode" "$work/data/mugen.cfg"
    rm -f "$work/mugen.log"
    rc=0
    (cd "$work"; timeout "${PERF_SECONDS:-20}" "./$bin" -nosound "$@" > /dev/null 2>&1) || rc=$?
    case "$rc" in 0|124) ;; *) echo "perf: $mode exited $rc" >&2; exit "$rc" ;; esac
    [ -s "$work/mugen.log" ] || { echo "perf: $mode produced no log" >&2; exit 1; }
    mv "$work/mugen.log" "$out/$mode.log"
done
sh "$root/tools/measure_perf.sh" "$out/OpenGL.log" "$out/System.log"
