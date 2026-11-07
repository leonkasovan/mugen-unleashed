#!/bin/sh
#
# measure_perf.sh -- summarise renderer performance from one or more mugen.logs.
#
# Usage:
#   sh tools/measure_perf.sh [-l label] logfile ...
#   sh tools/measure_perf.sh build/perf/gles/mugen.log
#
# Reads what the engine already writes:
#
#   perf: renderer=<name> fps=<n> frames=<n>    once a second, when
#                                               [Debug] ShowFPS = 1 is set
#   Total frames: N   Skipped frames: N         at the end of every match
#   Avg game rate / Avg frame rate              same place
#   Frames      : N / Time elapsed: <s> sec     at exit
#
# and prints one row per log:
#
#   scenario  renderer  fps avg  fps min  fps max  samples  drawn%  log
#
# No dependencies beyond awk, so it runs on the handheld itself.

usage()
{
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -gt 0 ] || usage

label=""
files=""
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        -l)        [ $# -ge 2 ] || usage
                   label=$2
                   shift 2 ;;
        -l*)       label=${1#-l}
                   shift ;;
        *)         files="$files $1"
                   shift ;;
    esac
done

printf '%-20s %-12s %-8s %-8s %-8s %-8s %-7s %s\n' \
       "scenario" "renderer" "fps avg" "fps min" "fps max" "samples" "drawn%" "log"

for f in $files; do
    if [ ! -f "$f" ]; then
        echo "measure_perf: no such file: $f" >&2
        continue
    fi

    # awk program kept free of apostrophes: the whole block is single-quoted by
    # the shell, and one stray quote in a comment breaks the script instead of
    # the data.
    awk -v file="$f" -v want="$label" '
        /perf: renderer=/ {
            name = ""; fps = 0
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^renderer=/) { name = substr($i, 10) }
                else if ($i ~ /^fps=/) { fps = substr($i, 5) + 0 }
            }
            if (name != "" && fps > 0) {
                if (renderer == "") { renderer = name }
                n++; sum += fps
                if (n == 1 || fps < lo) { lo = fps }
                if (n == 1 || fps > hi) { hi = fps }
            }
            next
        }
        /^Total frames:/ {
            half = split($0, part, "Skipped frames:")
            total = part[1]
            sub(/^Total frames:[ \t]*/, "", total)
            total = total + 0
            skipped = (half > 1) ? part[2] + 0 : 0
            next
        }
        /^Avg frame rate:/ { avgframe = $4 + 0; next }
        /^Avg game rate:/  { avggame = $4 + 0; next }
        /^Frames      :/   { exitf = $3 + 0; next }
        /^Time elapsed:/   { elaps = $3 + 0; next }
        END {
            scen = want
            if (scen == "") {
                nf = split(file, parts, "/")
                if (nf >= 2) {
                    scen = parts[nf - 1] "/" parts[nf]
                } else {
                    scen = file
                }
            }
            if (renderer == "") { renderer = "-" }

            drawn = "-"
            if (total > 0) {
                drawn = sprintf("%.0f%%", 100.0 * (total - skipped) / total)
            } else if (avgframe > 0 && avggame > 0) {
                drawn = sprintf("%.0f%%", 100.0 * avgframe / avggame)
            }

            if (n > 0) {
                printf "%-20s %-12s %-8.1f %-8.1f %-8.1f %-8d %-7s %s\n", \
                       scen, renderer, sum / n, lo, hi, n, drawn, file
            } else if (exitf > 0 && elaps > 0) {
                printf "%-20s %-12s %-8.1f %-8s %-8s %-8s %-7s %s\n", \
                       scen, renderer, exitf / elaps, "-", "-", "0", drawn, file
            } else {
                printf "%-20s %-12s %-8s %-8s %-8s %-8s %-7s %s\n", \
                       scen, renderer, "-", "-", "-", "0", drawn, file
            }
        }
    ' "$f"
done

cat <<'NOTE'

Columns:
  fps      Timer_GetFps(): frames per second over the last 31 frames, sampled
           once a second. This is the rate the loop is pacing at, capped by
           [Video] VRetrace and the display refresh, so every renderer reads
           about 60.0 when it is healthy.
  drawn%   (Total frames - Skipped frames) / Total frames. Below 100% means the
           loop fell behind and skipped drawing (Frame_UpdateSkip sets
           skip_draw when it is more than one frame late), so the perceived
           rate is lower than fps: 60 fps at 50% drawn presents 30 frames per
           second. Watch this number, not only fps.
  fps min  worst second in the run. One low sample beside a healthy average is
           a load spike (stage or character load, first frame after a screen
           change), not a steady-state cost.
  samples  0 means the log has no per-second lines: ShowFPS was off, so the fps
           column is the whole-run average from the exit statistics.

For per-second samples, put "ShowFPS = 1" in the [Debug] section of mugen.cfg.
NOTE
