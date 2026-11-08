#!/bin/bash
# PORTMASTER: mugen.zip, Mugen.sh
#
# Launcher for mugen-unleashed on ArkOS/PortMaster handhelds (aarch64).
#
# The engine binds the pad itself through SDL's game-controller API, and
# src/pads.c completes the mapping entry for a pad whose SDL database entry has
# no system buttons (the GO-Super Gamepad names only b0..b11, so SELECT, START
# and R3 were invisible). gptokeyb is therefore NOT used on purpose: PortMaster's
# helper grabs the pad and re-publishes it as a virtual device, which changes the
# GUID that fixup is keyed on.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

GAMEDIR="/$directory/ports/mugen"
cd "$GAMEDIR" || exit 1

# PortMaster convention: everything the launcher prints goes to log.txt. The
# engine writes its own mugen.log next to the binary, in the same directory.
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# If SDL ever reports a pad with the wrong buttons, PortMaster's own database can
# be handed to it. It is not used by default: the engine's semantic mapping plus
# the fixup in src/pads.c is what was verified on the RG351MP, and a database
# entry would replace that mapping wholesale.
# export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Two names are accepted because the build produces
# mugen-linux-arm64-<triple> while the port ships it as mugen-linux-arm64, and a
# launcher that silently ran the *old* file cost a whole debugging round: the
# one actually starting is logged, with a short hash.
BIN=""
for candidate in "mugen-linux-arm64" "mugen-linux-arm64-aarch64-none-linux-gnu"; do
  if [ -x "$GAMEDIR/$candidate" ]; then
    BIN="$candidate"
    break
  fi
done
if [ -z "$BIN" ]; then
  echo "error: no mugen-linux-arm64* binary in $GAMEDIR"
  command -v pm_finish >/dev/null 2>&1 && pm_finish
  exit 1
fi
echo "launcher: starting $BIN ($(md5sum "$BIN" | cut -c1-8))"

# Both helpers come from PortMaster's control.txt and are absent when the script
# is run by hand or over ssh; the game does not need them to start.
if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$GAMEDIR/$BIN"
fi
"./$BIN"

# Quit from the engine's own title screen (Exit), or with Select: Back/R3: Escape
# inside the game. pm_finish restores the front end either way.
command -v pm_finish >/dev/null 2>&1 && pm_finish
exit 0
