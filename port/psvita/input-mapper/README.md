# sdl-input-mapper — PS Vita input inspection tool

A small standalone PS Vita app that shows exactly what **SDL's Vita joystick
driver** reports: button indices, hat values and axis ranges.

It exists because mugen-unleashed's Vita port needed to map the gamepad onto
keyboard-driven menus, and M.U.G.E.N's menus read keyboard keys only — joystick
input goes into a separate set of pad input codes the system screens never
examine. Guessing which button index is Cross (or whether the d-pad arrives as
buttons or as a hat) produced a menu that could not be navigated. This tool
answers those questions by observation.

Output: **`sdl-input-mapper.vpk`**.

---

## What it shows

```
                        SDL INPUT MAPPER
             PS Vita  --  SDL 1.2 joystick, hats and axes

JOYSTICK: <the name SDL reports>
axes 4    buttons 12    hats 1

BUTTONS
[ 0 off] [ 1 off] [ 2 off] [ 3 ON ] ...        <- green when held

AXES  (raw value, -32768 .. 32767)
AXIS 0  |----------+--X--------|   -12345       <- bar with centre mark
AXIS 1  |----------+------------|        0

HATS  (raw SDL_HAT bits)          HAT 0 = 1

LAST EVENTS (most recent first)
BUTTON DOWN   index 10  value      1
AXIS          index  0  value -16384
...
```

* **Button cells** are labelled with the exact index SDL reports, and turn
  green while held. This is the number to use in a button mapping.
* **Axis bars** show the raw signed value and a centre mark, so you can see the
  resting position, the travel range, and which axis index is horizontal
  versus vertical.
* **Hats** show the raw `SDL_HAT_*` bit value (`UP=1 RIGHT=2 DOWN=4 LEFT=8`).
* **Last events** keeps the six most recent events in order, so a press can be
  tied to an index without reading fast.

Press **Escape** (or send `SDL_QUIT`) to exit.

## What the layout is expected to be

SDL's Vita joystick driver (`SDL_sysjoystick.o` in VitaSDK's `libSDL.a`)
reports **12 buttons, 4 axes, 0 hats**. The d-pad is not a hat — it is four
ordinary buttons — and the driver's `SCE_CTRL` bit mapping is:

| idx | bit | button | idx | bit | button |
|---|---|---|---|---|---|
| 0 | 0x1000 | Triangle | 6 | 0x40 | Down |
| 1 | 0x2000 | Circle | 7 | 0x80 | Left |
| 2 | 0x4000 | Cross | 8 | 0x10 | Up |
| 3 | 0x8000 | Square | 9 | 0x20 | Right |
| 4 | 0x100 | L | 10 | 0x1 | Select |
| 5 | 0x200 | R | 11 | 0x8 | Start |

Axes are the two sticks: 0 = left X, 1 = left Y, 2 = right X, 3 = right Y.

This tool exists to **confirm** that on a given console and SDL build, and to
catch it if a future VitaSDK changes the order. mugen-unleashed's Vita port uses
these indices directly (`src/events.c`), with Cross = cancel/back and
Circle = confirm following the console's own convention.

## The log file

Every event is also appended to plain text at:

```
ux0:data/sdl-input-mapper.log
```

It records the SDL version, the video surface, the joystick name and counts,
then one line per event:

```
sdl-input-mapper
SDL headers 1.2.16, linked 1.2.16
surface 960x544 16 bpp, flags 0xC0000020 (double buffered -> SDL_Flip required)
SDL_NumJoysticks() = 1
joystick 0: "PS Vita Controller" axes=4 buttons=12 hats=1
button index 10 DOWN   (SDL event 5)
button index 10 up     (SDL event 6)
axis   index  0 value -32768 (of -32768..32767)
hat    index  0 value 1 (UP=1 RIGHT=2 DOWN=4 LEFT=8 when set)
```

That file is the deliverable to send on if the on-screen layout is not enough:
it is a complete record of a session and needs no photograph.

`ux0:data` is used on purpose. The launch directory is not always writable, and
on the mugen port exactly that silently removed every diagnostic the port had.

---

## Building

```sh
export VITASDK=/c/vitasdk        # C:\vitasdk

cmake -S port/psvita/input-mapper -B build/psvita/input-mapper -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build/psvita/input-mapper
```

→ `build/psvita/input-mapper/sdl-input-mapper.vpk`

Install it with VitaShell (`select the .vpk → Install`), then launch it from the
LiveArea.

It is a separate CMake project from the game on purpose: it needs only SDL from
VitaSDK, linked by explicit name, so it builds without the game's sources, its
data tree, or the pkg-config setup that `port/psvita/CMakeLists.txt` has to
arrange.

### Options

| Option | Default | Meaning |
|---|---|---|
| `MUGEN_VITA_TITLEID` | `MSDK00002` | 9-character title ID (differs from the game's) |
| `MUGEN_VITA_VERSION` | `01.00` | `APP_VER` |

---

## Layout of the source

```
port/psvita/src/
  sdl_input_mapper.c            the tool
  input_mapper_font.h           embedded 5x7 bitmap font (generated)
  vita_newlib_inputmapper.c     _newlib_heap_size_user
port/psvita/input-mapper/
  CMakeLists.txt                this project
```

The font is embedded rather than loaded: the tool must run with no game data
present, so it cannot depend on `data/` or `font/` or on SDL_ttf plus a `.ttf`.
It is a 5x7 table drawn with `SDL_FillRect`, merging set pixels into horizontal
runs so a full redraw stays cheap.

## Three Vita SDL behaviours it works around

These are the same traps that cost the mugen port several rounds, so the tool
avoids them from the start:

1. **`SDL_SWSURFACE`, and `SDL_Flip` every frame.** The Vita driver returns a
   double-buffered surface (`flags 0xC0000020`) whether or not `SDL_DOUBLEBUF`
   was requested, and SDL 1.2 presents such a surface only on `SDL_Flip`.
   Without it the screen stays black while everything else works.
2. **Video is initialised without audio.** SDL 1.2's `SDL_Init` walks its
   subsystems in a fixed order and returns at the first failure; audio comes
   before video, so a failed audio init leaves video uninitialised and the next
   `SDL_SetVideoMode` fails for no visible reason. This tool does not ask for
   audio at all.
3. **`_newlib_heap_size_user` is defined.** VitaSDK's crt0 defaults the newlib
   heap to 128 MiB, which exceeds what a SELF may allocate, and then every
   allocation returns NULL. See `vita_newlib_inputmapper.c`.

`SDL_JoystickEventState(SDL_ENABLE)` is also required: without it SDL reports no
joystick events at all, which is an easy half-hour to lose.
