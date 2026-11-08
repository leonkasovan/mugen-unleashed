# SDL2 gamepad detection on Windows

The win32 and win64 SDL2 builds share `src/pads.c`, the SDL2 event path, and
the per-player configuration policy in `src/config.c`. Detection does not use
pointer-sized integer casts or depend on the reconstructed 32-bit `Pad` layout.

## Back and Pause controls

Mapped SDL2 controllers use separate system controls, outside the eleven
configurable gameplay actions. No new `mugen.cfg` entries are needed.

| Context | SELECT / Back | R3 / right-stick click |
|---|---|---|
| Options, character selection and other screens | Escape / Back | Escape / Back |
| Keyboard or joystick button configuration | Cancel the current assignment and restore its previous bindings; press again to leave | Same |
| Match, including training | Pause / resume | Escape: leave the match using the existing keyboard path |
| Main title / mode select | No action | Close the info panel first, otherwise highlight Exit when that item exists |

At the main title, highlighting Exit never activates it: confirm separately
with an ordinary menu button. Keyboard Escape keeps its existing behavior.
Controllers without R3 can still use SELECT to leave menus and to pause or
resume fights; keyboard Escape remains available to leave a match. No
SELECT+START chord is reserved, so START retains its gameplay/palette role.

System presses are edge-triggered. Releases and repeated button-down events
do not trigger another action or confirm an item on the next screen. SELECT
and R3 do not enter the gameplay callback or held-button state. A raw joystick
without an SDL controller mapping retains its existing button 12/13 meanings.
The decoder handles all four slot offsets, rather than recognizing P1 only.

`tools/test_gamepad_config.sh` exercises production SDL2 button dispatch,
`Screen_ReadInput`, `Game_ReadInput`, `Title_Update`, and `KeyConfig_Run` with
virtual mapped P1/P2 controllers. It checks pause/resume, Escape, safe title
exit, info-panel closure, a hidden Exit item, release/repeat suppression,
rollback of partial keyboard/joystick assignments, ordinary attacks, and raw
joystick fallback. Rendering, event scheduling and input-map rebuilding remain
stubbed boundaries. Both desktop and Android compile paths passed on Linux
x86-64 with ASan/UBSan (leak detection disabled for the sandbox). The mock
pad suite also checks all four slot offsets on x86-64 and i386 through QEMU.
Removing slot normalization makes the P2 pause assertion fail. This is host
coverage, not Windows hardware verification.

## What the reported log establishes

The reported win64 log reaches `SDL_NumJoysticks() = 0` and then disables
runtime joystick input. This happens before controller button mapping. Changing
`[P1 Gamepad]` or `[P2 Gamepad]` cannot make an unenumerated device appear.
The old log did not record SDL initialization failures, its runtime version,
or input driver hints. It therefore does not establish why this Windows
machine reports zero devices, or prove a bitness-specific cause.

## Corrections

* Check initialization errors and initialize raw joystick input independently
  from the optional game-controller service. A failed combined initialization
  must not discard otherwise usable raw input. Log each failure immediately.
* Reuse already initialized services, track references acquired by the pad
  module, and release only those references during teardown. Repeated empty
  scans must not accumulate SDL subsystem references.
* Pump SDL events on the main/video thread before counting devices. This
  refreshes input discovery without consuming queued input events.
* Treat a negative enumeration result as an error, and log open failures with
  the device index and SDL error. Keep the global count nonnegative.
* Publish raw bindings when an unmapped joystick attaches after a startup
  without devices. Re-enable runtime input from the saved joystick preference
  for both raw and mapped devices. Honor `-nojoy` and a disabled preference.
* Record SDL runtime/header versions, revision, pointer width, subsystem state,
  driver/filter hints, and each opened device's GUID and instance ID. A hint
  value is configuration, not proof that its driver was compiled into SDL.

These changes preserve stored raw and semantic button mappings. SDL 1.2 keeps
its existing lifecycle; the initialization and diagnostic additions are SDL2
only.

SDL documents [initialization errors](https://wiki.libsdl.org/SDL2/SDL_InitSubSystem),
[subsystem reference counting](https://wiki.libsdl.org/SDL2/SDL_Init), and the
[main-thread event-pump requirement](https://wiki.libsdl.org/SDL2/SDL_PumpEvents).

## Rebuild and collect evidence

Use the matching static dependency kit in separate shells. See
[BUILD-MINGW.md](BUILD-MINGW.md) and the Makefile for kit requirements.

```sh
# 32-bit shell
export PATH=/c/x86devkit/bin:$PATH
make MUGEN_TARGET=win32 SDL_BACKEND=2 check-toolchain
make -j8 MUGEN_TARGET=win32 SDL_BACKEND=2
make MUGEN_TARGET=win32 SDL_BACKEND=2 install
```

```sh
# 64-bit shell
export PATH=/c/w64devkit/bin:$PATH
make MUGEN_TARGET=win64 SDL_BACKEND=2 check-toolchain
make -j8 MUGEN_TARGET=win64 SDL_BACKEND=2
make MUGEN_TARGET=win64 SDL_BACKEND=2 install
```

Connect the controller before starting the rebuilt executable. Save its new
`mugen.log` separately for each architecture. Also test attaching it after the
options/menu screen appears. Do not reset the user's config to diagnose a
zero-device count.

| New log result | Interpretation / next action |
|---|---|
| `joystick init failed: ...` | Use the actual SDL error to investigate initialization or the linked SDL build. |
| `controller init failed: ...` | Raw enumeration remains available; investigate the controller-service error separately. |
| `joystick enumeration failed: ...` | Enumeration returned an error, rather than a valid empty list. |
| Subsystems initialized, `SDL_NumJoysticks() = 0` | SDL still sees no devices. Check Windows/driver visibility and the logged driver/filter configuration. |
| `SDL_JoystickOpen(n) failed: ...` | SDL found a device but could not open it; use that error. |
| Opened device, `mapped=0` | Detection succeeded. The raw `[P<n> Joystick]` bindings apply; semantic `[P<n> Gamepad]` requires an SDL mapping. |
| Opened device, `mapped=1` | Detection succeeded; inspect per-player enabling and `[P<n> Gamepad]` bindings if controls do not respond. |

If both rebuilt executables still enumerate zero devices, provide both new
logs, the controller's manufacturer/model, and its connection type (USB,
Bluetooth, or receiver). Confirm whether Windows itself can see the device.
Do not blindly force XInput, DirectInput, HIDAPI, RawInput or WGI hints: the
appropriate backend depends on the actual hardware and SDL build.

## Regression evidence and limits

`tools/test_pad_detection.sh` exercises the production pad module against a
deterministic SDL boundary: joystick/controller initialization failures,
repeated scans, external service ownership, negative enumeration, failed opens,
and discovery during an event pump. It passed on Linux x86-64 with ASan/UBSan
(leak detection disabled for the sandbox) and Linux i386 through QEMU. The
32-bit harness asserts its pointer width. These are not Windows runtime tests.

`TEST_PLATFORM=desktop bash tools/test_gamepad_config.sh` and its default
Android compile path passed against real SDL2 virtual raw joysticks and mapped
controllers on Linux x86-64. They exercise production config loading, hotplug,
the options menu, saving/reloading mappings, and preservation of comments,
section uniqueness, BOM and line endings. The new late-raw case reproduces
the no-device runtime state at the harness's stubbed `Input_InitPads` boundary;
it does not claim end-to-end startup coverage. Restoring the old
`Config_ApplyJoystickSlot` makes that assertion fail.

The changed production units also passed Linux syntax checks with SDL2
(`-Werror`), the Android SDL2 defines, and SDL 1.2 headers. These checks do not
replace a complete engine build or platform link.

No physical Windows controller, win32/win64 executable execution, MSVC build,
Android device, or Vita device was available for this change. The original
Windows zero-device cause remains open until the new diagnostics are collected.
