# arm64 Linux port (cross-compiled from Windows)

Builds the engine for **Linux arm64** from a Windows host, with Arm's GNU
toolchain. No arm64 machine, no chroot, no WSL build required.

Two entry points, same job. `build.bat` runs in **cmd.exe** and needs nothing
but a devkit: it takes `make`, `sh`, `sed`, `grep` and `strings` from
`w64devkit`/`x86devkit` and everything else from Windows itself (curl,
PowerShell for the checksums, System32's bsdtar for the archives). The kit's
busybox `sh` is handed to make as its `SHELL`, which is what lets the POSIX
recipes run without MSYS2 or Git Bash anywhere in the picture.

```bat
port\arm64\build.bat --setup     :: once: download the toolchain (~330 MB)
port\arm64\build.bat             :: build
```

From PowerShell:

```powershell
.\port\arm64\build.ps1 -CheckTools      # validate, build nothing
.\port\arm64\build.ps1 -Jobs 8          # build, GLES by default
```

`build.ps1` is the PowerShell front end for the same build: `-CheckTools`,
`-Jobs N`, `-Debug`, `-Clean`, `-Offline`, `-NoData`/`-Data DIR` and friends,
and anything unrecognised is passed through, so `\build.ps1 --gles 0` works
too. It forwards to `build.bat` -- one implementation, two front doors -- and
PowerShell may need `-ExecutionPolicy Bypass` (or `Unblock-File build.ps1`)
before it will run a local script.

→ `build/linux-arm64-aarch64-none-linux-gnu-sdl2/mugen-linux-arm64-aarch64-none-linux-gnu`

This is the same `linux-arm64` target `make MUGEN_TARGET=linux-arm64` builds
natively on Linux; the port only supplies the toolchain and the dependencies.

## Pick the toolchain by the device's glibc

A binary cannot require a newer glibc than the device has, and the mismatch is
not subtle — the loader refuses it before any engine code runs:

```
./mugen: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.38' not found
```

So ask the device first:

```sh
ssh ark@<device> 'ldd --version | head -1'
# ldd (Ubuntu GLIBC 2.30-0ubuntu2.2) 2.30        <- RG351MP / ArkOS
```

then build with the toolchain that matches:

| `ARM64_TOOLCHAIN=` | Compiler | glibc | For |
|---|---|---|---|
| `9.2` (default) | gcc 9.2.1 | **2.30** | RG351MP/ArkOS and other 2.30-era handhelds — **verified** |
| `15.3` | gcc 15.3.1 | 2.42 | a current distro; needs glibc ≥ 2.38 on the target |

```sh
port\arm64\build.bat                       # 9.2, glibc 2.30
ARM64_TOOLCHAIN=15.3 port\arm64\build.bat  # 15.3, glibc 2.42
```

`--check-tools` prints the installed glibc next to the requested one and says so
when they disagree; `--setup` replaces a toolchain of the other version rather
than overlaying them.

**Verified on an RG351MP (ArkOS, kernel 4.4.189, Mali-G31):** the 9.2 build
initialises to `renderer: requested=opengl effective=opengl` on `GLES 3.2` and
plays a full match with no errors; the 15.3 build is rejected by the loader.

| | needed by the 9.2 build | device has |
|---|---|---|
| glibc | up to `GLIBC_2.29` | 2.30 |
| SDL2 | shared, 2.0.x | 2.0.10 |
| SDL2_mixer, SDL2_ttf | shared | 2.0.4, 2.0.15 |

## What it needs, and what the build scripts do about it

| Need | Handled by |
|---|---|
| A shell | `build.bat` uses only the devkit's own busybox `sh` (as make's `SHELL`) and cmd.exe — no MSYS2, no Git Bash, no WSL. `build.bat` is the MSYS2/Git Bash equivalent. |
| An `aarch64-none-linux-gnu` compiler | `--setup` downloads Arm's Mingw-w64 build and verifies it against the vendor's published checksum (MD5 for the 9.2 series, SHA-256 for 15.3). Installed into `TOOLCHAIN_DIR` (default `C:\arm-gnu-toolchain-<version>`). |
| SDL2, SDL2_mixer, SDL2_ttf, zlib, libpng, FreeType for aarch64 | downloaded as `arm64-sysroot.zip` (a pinned, SHA-256-verified release asset) and unpacked into `build/sysroots/arm64-sysroot/`. |
| pkg-config | the kit's, with its search path replaced by the tree above (`PKG_CONFIG_LIBDIR` and `PKG_CONFIG_PATH` are both cleared, so a host SDL2 cannot leak in). |

The Arm toolchain ships a complete glibc but **no SDL**: it is a bare
`--target=aarch64-none-linux-gnu` toolchain, not a distribution root. That is why
the dependency tree exists, and why it is published as an asset instead of being
assembled on the build machine.

## Usage

The two entry points take the same flags; the examples use `build.bat`, which is
the one that runs in cmd.exe. Under MSYS2/Git Bash, read `build.bat` for the same
thing.

```
port\arm64\build.bat                 download anything missing, then build
port\arm64\build.bat --setup         download the toolchain, then stop
port\arm64\build.bat --check-tools   report what is present, build nothing
port\arm64\build.bat --sysroot       re-download the dependency tree, then build
port\arm64\build.bat --clean         remove this port's build output
port\arm64\build.bat --no-sysroot    use the existing tree as-is
port\arm64\build.bat --offline       fail instead of downloading
port\arm64\build.bat --jobs N        parallel make jobs (default: CPU count)
port\arm64\build.bat --gles 0 or 1   renderer; 1 = GLES 3.0 (the arm64 default)
port\arm64\build.bat --model3d       3D stage models ([BGdef] model = *.glb); on by default
port\arm64\build.bat --no-model3d    compile model support out
port\arm64\build.bat --debug         -O0 -g3 -DMUGEN_DEBUG into a separate tree, and
                                     turns on the batch-rendering profiler (`batch:`
                                     line in mugen.log, next to `perf:`)
port\arm64\build.bat --upload        build, then copy the binary to the device
port\arm64\build.bat --no-keep-old   do not keep the previous binary as .prev
port\arm64\build.bat --device U@H    device to upload to (default ark@192.168.7.2)
port\arm64\build.bat --device-dir D  device folder (default mugen, i.e. ~/mugen)
```

Environment overrides:

| Variable | Default | Meaning |
|---|---|---|
| `TOOLCHAIN_DIR` | `C:\arm-gnu-toolchain-<version>` | where the toolchain is installed |
| `ARM64_TOOLCHAIN` | `9.2` | which Arm release: `9.2` (glibc 2.30) or `15.3` (glibc 2.42) |
| `ARM64_SYSROOT_URL` | the `toolchains-v1.0` release asset | where the dependency tree comes from |
| `ARM64_SYSROOT_SHA256` | pinned in the scripts | its expected hash; needed when the asset is republished |
| `MUGEN_GLES` | `1` | same as `make MUGEN_GLES=`; `--gles` wins |
| `MUGEN_WIN_KIT` | autodetected | `build.bat` only: which devkit supplies make/sh/sed (`C:\w64devkit`, `C:\x86devkit`, then PATH) |
| `DEVICE` | `ark@192.168.7.2` | device for `--upload` |
| `DEVICE_PASS` | *(prompted)* | its password; `build.bat` uses it via `sshpass`, `build.bat` needs a key or an interactive prompt |
| `DEVICE_DIR` | `mugen` | target folder on the device (relative to `~`) |

`--setup` is the only thing that writes outside the repository, and only into
`TOOLCHAIN_DIR`. The download is cached at
`build/downloads/`, so a re-run does not transfer it again.

## Uploading to the device

```sh
DEVICE_PASS=ark port\arm64\build.bat --upload
```

```
==> Uploading to ark@192.168.7.2:mugen/
    logged in
    sending 8 MB
    checksum verified (5fa3ebc35e8ad185304fa5147715adbe)
    previous binary kept as mugen-linux-arm64.prev
    ark@192.168.7.2:mugen/mugen-linux-arm64  (9392992 bytes)
```

What it does, and why:

- **Transport is `tar` over the ssh exec channel, not scp or sftp.** This
  device's sshd points its `sftp` subsystem at
  `/usr/local/crosware/.../dropbear/current/libexec/sftp-server`, which does not
  exist, so both `scp` and `sftp` fail against it while plain `ssh` works.
  `tar cf - | ssh "tar xf -"` needs nothing on the device beyond tar.
- **Verified before replacing.** The archive is unpacked to
  `.mugen-linux-arm64.new` and the md5 compared with the local file; only a
  match renames it into place. A dropped connection cannot leave a truncated
  binary where a working one was.
- **The previous binary is kept** as `mugen-linux-arm64.prev` (the `.prev`
  convention already used on the device), so a bad build rolls back on the
  device itself: `mv mugen-linux-arm64{.prev,}`. `--no-keep-old` skips it.
- Only the binary is uploaded. Game data (`data/`, `chars/`, …) is yours; it is
  not touched. For the GLES renderer the device also needs
  `data/gles/*.{vert,frag}` — `make install MUGEN_GLES=1` copies them, or copy
  `res/shaders/gles/` across by hand.

`sshpass` is required locally (MSYS2: `pacman -S sshpass`). Without
`DEVICE_PASS` the password is prompted for.

## PortMaster launcher

`ports/` holds the three files a PortMaster-style front end needs, so the port
can be packaged the same way as any other:

| File | Goes to | What it is |
|---|---|---|
| `ports/Mugen.sh` | `/roms/ports/Mugen.sh` | the launcher: PortMaster boilerplate, then `mugen-linux-arm64` from `/roms/ports/mugen` |
| `ports/gameinfo.xml` | `/roms/ports/mugen/` | the front-end entry (name, description, screenshot) |
| `ports/port.json` | `/roms/ports/mugen/` | PortMaster metadata (title, porter, arch, glibc floor) |

Two things in the launcher are deliberate:

- **No `gptokeyb`.** The engine binds the pad itself through SDL's
  game-controller API, and `src/pads.c` completes the SDL mapping entry for a pad
  whose database entry has no system buttons (the GO-Super Gamepad names only
  `b0..b11`). PortMaster's helper grabs the pad and re-publishes it as a virtual
  device, which changes the GUID that fixup is keyed on.
- **Both binary names are accepted** and the one started is logged with a short
  hash — `launcher: starting mugen-linux-arm64 (2cc89c08)`. A launcher that
  silently ran an older file while the new build sat next to it cost a debugging
  round once; the log line makes that impossible to miss.

## The dependency tree

`build.bat` downloads `arm64-sysroot.zip` from this project's `toolchains-v1.0`
release into `build/sysroots/arm64-sysroot/`, keeping the archive in
`build/downloads/` as a cache. The SHA-256 is pinned in `build.bat` and
checked on every use, so a truncated or replaced download cannot become the tree
that gets linked:

```
arm64-sysroot.zip   15.8 MB  ->  39 MB unpacked, 273 files
sha256 16bbbf25b83b770f92ab478e330ea9eca0351c1f5792037ba958f02bece65049
```

`ARM64_SYSROOT_URL` and `ARM64_SYSROOT_SHA256` override both, for a fork or a
re-published asset. The tree holds SDL2, SDL2_mixer, SDL2_ttf, zlib, libpng16 and
FreeType as shared objects, their headers, hand-written `.pc` files and the
link-time closure in `usr/lib/rpath/` — see `docs/BUILD-LINUX.md` for what is in
it and why. It was assembled once from an aarch64 Ubuntu 22.04 root and is the
same for every host; nothing in this repository regenerates it, because that
needs an aarch64 root filesystem most checkouts do not have.

An install is uploaded only after its checksum, and it is unpacked beside the
destination (`arm64-sysroot.mugen-new`) and swapped in, so a failed download or a
half-extracted zip leaves the working tree in place. Every `.pc` `prefix=` is
rewritten to `${pcfiledir}/../..` on install, so the tree works from any path.

To rebuild and republish the tree (new library versions, another distro root),
see "Updating the tree" in `docs/BUILD-LINUX.md` — the generator lives in git
history, not in the tree.

## Limitations

**The glibc floor, and how to move it.** Handled by `ARM64_TOOLCHAIN` (above),
but the general rule is worth stating because a newer toolchain is always the
tempting default:

- `ARM64_TOOLCHAIN=15.3` (glibc 2.42) produces a binary wanting `GLIBC_2.38`,
  which nothing older than a 2024 distribution has.
- `ARM64_TOOLCHAIN=9.2` (glibc 2.30) produces one wanting `GLIBC_2.29`, which is
  what an ArkOS handheld can run.

Check any binary before shipping it:

```sh
readelf -V <binary> | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1
```

If the device is older than even 2.30, the remaining options are to build
natively on the target (or in an arm64 chroot) with its own gcc, or to use a
compiler from a root that matches. Older Arm releases are no longer on GitLab
(which starts at 11.2); they are on `developer.arm.com/downloads`, under
`/-/media/Files/downloads/gnu-a/<version>/binrel/`, and redirect to
`armkeil.blob.core.windows.net`. Note that those archives publish **MD5**, not
SHA-256, despite the `.sha256asc` file name.

**Not a static binary.** SDL2, SDL2_mixer and SDL2_ttf are linked as shared
objects, because that is what an arm64 root provides. The target needs them
installed (they are what its distribution packages anyway). `--sysroot`/
`XSYSROOT` only changes what the *build* links against, not that.

**The GLES renderer needs a GPU.** `linux-arm64` defaults to `MUGEN_GLES=1`
because the handhelds it targets expose OpenGL ES only. If the device cannot
create an ES 3.0 context, the engine falls back to the System renderer and says
why in `mugen.log`; `MUGEN_GLES=0` builds the desktop-GL one instead.

## Verifying a build without an arm64 device

```sh
# WSL with qemu-user, or any Linux host with binfmt_misc
cd install
LD_LIBRARY_PATH=/path/to/aarch64/libs \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  qemu-aarch64 -L /path/to/arm64/sysroot ./mugen-linux-arm64-aarch64-none-linux-gnu \
  -nosound -p1 kfm -p2 kfm
```

Set `RenderMode = System` in `install/data/mugen.cfg` first: the dummy video
driver has no OpenGL, so the default `OpenGL` mode stops at
`SDL_CreateWindow failed: OpenGL support is either not configured...`. Restore
the config and remove the copied binary afterwards — `install/` is the pristine
game tree.

## Layout

```
port/arm64/
  build.bat              entry point: toolchain, dependency tree, make
  README.md             this file
```

The `Makefile` itself knows nothing about Arm or Windows: `CROSS_COMPILE=` and
`XSYSROOT=` are generic, so another platform port can reuse them with a
different prefix and tree. What the cross build changes in the Makefile, and
why, is in `docs/BUILD-LINUX.md` ("What the Makefile does differently").

### Recovery and replacement safeguards

`--setup` verifies and unpacks a replacement before changing an installed
compiler. It prepares a sibling `.mugen-new` directory, moves a managed old tree
to `.mugen-old`, and restores that tree if the final install rename fails.
An unmarked, nonempty toolchain directory is left alone: select a new
`TOOLCHAIN_DIR` instead. Existing `.mugen-new`/`.mugen-old` directories indicate
an interrupted transaction and are not silently deleted.

A compiler that is present but **will not run** (a native Windows shell cannot
execute a `/c/.../gcc.exe`) is not treated as missing: the script stops and says
so rather than downloading a fresh toolchain over a working one. Only `--setup`
replaces an installed tree.

The build stamp records the resolved compiler path, version and dependency-root
path. Switching between the versioned 9.2 and 15.3 kits therefore rebuilds the
objects and executable even though their cross prefixes are identical.

Upload archives carry the temporary remote filename from the outset. A checksum
failure leaves the existing executable intact even when `DEVICE_BIN` equals the
local build filename. No executable is renamed into its final location until
verification succeeds. This safeguard does not upload GLES shader assets; the
separate shader installation requirement above still applies.
