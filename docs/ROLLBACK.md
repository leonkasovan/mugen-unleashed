# GGPO rollback

Build the Linux executable with a C++ compiler and the usual SDL2 build dependencies.

```sh
make -j8 MUGEN_ROLLBACK=1
```

The x86-64 executable is `build/linux-x64-sdl2/mugen-linux-x64`.
Run it from a compatible game data directory, such as `install/`.
For the RG351MP cross-build and device test commands, see the
[ARM64 port guide](../port/arm64/README.md#ggpo-rollback-on-rg351mp).

## Local determinism test

From `install/`, run a scripted Kung Fu Man mirror match with rollback every four frames.
GGPO compares the checkpoints from original and replayed frames within the same process.

```sh
SDL_VIDEODRIVER=dummy ../build/linux-x64-sdl2/mugen-linux-x64 \
  -p1 kfm -p2 kfm -s kfm -rollbacktest 720
```

Success is exit status 0 and `Rollback test passed: 720/720 frames, 720 resimulated`
in `mugen.log`. A checksum mismatch, storage change, or match that ends before
the requested frame count fails the test.

## Direct UDP match

Run the following commands from two copies of the game data directory on one
machine for loopback testing. For a LAN match, replace `127.0.0.1` in each
command with the other player's IPv4 address. Each copy should use the same
executable build, game data, config, characters, stage and match options.
`-sessionseed` defaults to `424242` in rollback modes.

```sh
# Player 1
../build/linux-x64-sdl2/mugen-linux-x64 \
  -p1 kfm -p2 kfm -s kfm -rounds 1 -rollback 1 7100 127.0.0.1 7101

# Player 2
../build/linux-x64-sdl2/mugen-linux-x64 \
  -p1 kfm -p2 kfm -s kfm -rounds 1 -rollback 2 7101 127.0.0.1 7100
```

Both peers use their local P1 controls. Check `mugen.log` for synchronization,
the number of resimulated frames and the confirmed match result. A clean local
connection may finish with zero resimulated frames.

The current scope is experimental 1v1 quick VS with CNS characters and 2D
sprite stages. Rollback sessions run with sound and music disabled and use
direct IPv4 endpoints. The startup check compares basic match settings. Both
peers must still use the same assets because the check does not hash game files.
The engine aborts if registered fight-state storage moves or exceeds the 32 MiB
checkpoint budget. Hardware and cross-platform determinism are unverified.

The GGPO callbacks save and restore the in-process fight state and advance it
from synchronized inputs. Replayed frames skip rendering and presentation.
The peers exchange inputs and a setup key. See the
[Ikemen Go reference](https://github.com/RollinJay/IkemenGo-Nakama/blob/5fd8ecf36a7ac3e383c93a1afeb0125711fe5082/src/rollback.go)
and the [vendored GGPO notes](../third_party/ggpo/README.mugen.md).
