# mugen-unleashed -- GNU Make + GCC (MinGW-w64, 32-bit) STATIC build.
#
# Produces a self-contained engine binary: the engine in src/, the libraries
# in third_party/ built from their original sources, libresample vendored, and
# SDL2 + SDL2_mixer + SDL2_ttf + zlib + pthread linked statically from the
# x86devkit (SDL2_mixer brings mpg123, vorbisfile, FLAC, opus and wavpack with
# it, so MP3/Ogg playback needs no external decoder DLL). No
# Elecbyte.MUGEN.libs and no plugin DLLs are needed to run.
#
# SDL2-only Windows/Linux build; Android and Vita retain platform CMake.
#
# The Windows reference build is a 32-bit GUI executable and needs an
# i686-w64-mingw32 toolchain (x86devkit): the engine is a 32-bit binary
# reconstruction, its struct layouts and pointers are 4 bytes. An
# x86_64-w64-mingw32 toolchain (w64devkit) builds the experimental win64
# target instead. Which one is built is decided by the compiler that is
# actually on PATH -- the Makefile asks it for its own triple -- so neither
# needs a variable; see "platform detection" below.
#
# The same Makefile also builds a Linux target, chosen from uname and the
# compiler's triple (docs/BUILD-LINUX.md). There the distro's shared SDL is
# linked instead of the kit's static archives, so the executable is
# mugen-<target> and both checks below follow the platform being built.
#
#   make              build $(TARGET): mugen-mingw-<target>-sdl2.exe on
#                     Windows, mugen-<target> on Linux (SDL2 is the only
#                     backend there)
# (verify with "make help")
#   make -j8          parallel build
#   make install      strip and copy the executable into install/, and install
#                     res/mugen.cfg as its data/mugen.cfg when the tree has none
#   make V=1          show the full command lines
#   make clean        remove the object files of $(BUILD)
#   make distclean    remove $(BUILD) entirely
#   make check-toolchain   verify the compiler and the libraries this target needs
#   make validate     build tools/validate, the asset-path checker (see below);
#                     it has its own section with the cross-compile line
#   make sffcli       build the standalone SFF inspector, PNG exporter and optimizer
#
# The window icon comes from res/icons/*.ico (see "the window icon" below).
#
# Optional:
#   make MUGEN_GAME_DIR=C:/mugen-1.1b1    embed the original mugen.exe's icon
#                                         instead (needs python3;
#                                         tools/extract_icon.py)
#   make MUGEN_ICONS=res/icons/1.ico      a different icon, or several to merge
#   make MUGEN_CFG_FORCE=1                replace install/data/mugen.cfg, which
#                                         is otherwise the player's to keep
#   make MUGEN_CFG=<path>                 install a different config file
#   make BUILD=build/mingw-debug DEBUG=1  unoptimised build with debug info
#   make MUGEN_GLES=0                     force the desktop-GL renderer on a
#                                         Linux GLES target (see below)
#   make CROSS_COMPILE=aarch64-none-linux-gnu- XSYSROOT=build/sysroots/arm64-sysroot
#                                         cross-compile the Linux target with a
#                                         foreign toolchain + dependency tree;
#                                         port\arm64\build.bat wraps this
#                                         (docs/BUILD-LINUX.md, "Cross-compiling")
#
# See docs/BUILD-MINGW.md for what the dependencies are and where they come from.

# ---------------------------------------------------------------- configuration

# GNU Make predefines CC=cc with origin "default", so `CC ?= gcc` never took
# effect: the build silently used whatever `cc` happened to be. Pick gcc only
# when nobody else chose, so CC=... on the command line or in the environment
# (the cross-compile case) still wins.
ifeq ($(origin CC),default)
  CC := gcc
endif
AR       ?= ar
WINDRES  ?= windres
STRIP    ?= strip
PKG_CONFIG ?= pkg-config

# ------------------------------------------------------------- cross-compiling
#
# CROSS_COMPILE=<prefix> runs the Linux build with a foreign toolchain
# (aarch64-none-linux-gnu-, arm-linux-gnueabihf-, ...) and XSYSROOT=<dir> names
# the dependency tree that toolchain links against -- the SDL2/SDL2_mixer/
# SDL2_ttf/zlib/libpng headers, shared objects and .pc files, laid out as
# <XSYSROOT>/usr/{include,lib/pkgconfig,lib}. The toolchain directory itself is
# never written to.
#
# This block is deliberately generic: it knows nothing about a particular
# toolchain or host. Each platform port owns its own wiring, and for arm64 that
# is port/arm64/ -- it downloads the toolchain and builds the dependency tree,
# then calls this Makefile with the two variables below.
#
#     port\arm64\build.bat             (Windows host, Arm GNU Toolchain)
#
# Equivalently, by hand:
#
#     make CROSS_COMPILE=aarch64-none-linux-gnu- \
#          XSYSROOT=build/sysroots/arm64-sysroot MUGEN_TARGET=linux-arm64
#
# Full recipe, and what a first attempt gets wrong and why:
# docs/BUILD-LINUX.md, "Cross-building arm64 from Windows".
#
# Everything the cross build changes, and why:
#
#   CC/AR/STRIP   the prefix is applied to whatever was chosen, so the default
#                 host `gcc` cannot leak in.
#   BUILD name    build/linux-arm64-<prefix> otherwise, so a cross tree can
#                 never share objects with the host one (same triple, different
#                 ABI features).
#   -isystem      XSYSROOT/usr/include first and -- on a real cross build -- with
#                 -nostdinc, so the host's /usr/include (x86_64 SDL2, glibc) is
#                 not visible at all. Only correct because the tree carries
#                 every include the compile needs.
#   --sysroot     the toolchain's glibc sysroot when the compiler does not carry
#                 one of its own.
#   PKG_CONFIG    the host pkg-config, with its search path *replaced* by
#                 $(XSYSROOT) (PKG_CONFIG_LIBDIR), so the host's .pc files cannot
#                 be picked up -- they carry the same package names.
#   --static      asked of pkg-config, so each package's own link line (SDL2's
#                 -lm -ldl -lpthread, libpng's -lz) is carried along.
#
# `make check-toolchain` reports the resolved cross configuration.
ifneq ($(strip $(CROSS_COMPILE)),)
  CC      := $(CROSS_COMPILE)gcc
  AR      := $(CROSS_COMPILE)ar
  STRIP   := $(CROSS_COMPILE)strip
  MUGEN_CROSS := 1
  # A cross toolchain with no dependency tree cannot build this engine, and the
  # failure without this check is silent rather than loud: pkg-config answers
  # from the *host* (an x86_64 SDL2 with the same .pc names), the compile then
  # picks up host headers and the link mixes architectures. Refuse instead.
  ifeq ($(strip $(XSYSROOT)),)
    $(error CROSS_COMPILE=$(CROSS_COMPILE) needs XSYSROOT=<dir>, the tree holding the target's \
            SDL2/zlib/libpng headers, libraries and .pc files; \
            use the platform port (port\arm64\build.bat) -- see docs/BUILD-LINUX.md)
  endif
  ifneq ($(strip $(XSYSROOT)),)
    MUGEN_CROSS_SYSROOT := $(abspath $(XSYSROOT))
    MUGEN_CROSS_INC     := $(MUGEN_CROSS_SYSROOT)/usr/include
    MUGEN_CROSS_LIB     := $(MUGEN_CROSS_SYSROOT)/usr/lib
    MUGEN_CROSS_STATIC  := --static
    # Whether the compiler already carries its own glibc sysroot. Arm's
    # aarch64-none-linux-gnu does (built --with-sysroot); a bare cross gcc on a
    # Linux host does not and needs --sysroot=$(XSYSROOT). Asked here, at the
    # top, because CROSS_CPPFLAGS below is decided from it.
    MUGEN_CROSS_COMPILER_SYSROOT := $(shell XSYSROOT=x $(CC) -print-sysroot 2>/dev/null)
    ifeq ($(strip $(MUGEN_CROSS_COMPILER_SYSROOT)),)
      MUGEN_CROSS_SYSROOT_FLAGS := --sysroot=$(MUGEN_CROSS_SYSROOT)
    endif
    # The empty file the search list is asked for. /dev/null is a POSIX path:
    # the kit's busybox sh can write to it, but a Mingw-w64 gcc.exe is a native
    # binary that cannot open it and then prints no search list at all -- the
    # build died on `#include <math.h>` from cmd.exe. NUL is what Windows wants.
    MUGEN_CROSS_NULL := $(if $(wildcard /dev/null),/dev/null,NUL)
    # -nostdinc below drops *every* implicit include directory, the toolchain's
    # own C library headers among them (math.h, stdio.h, ...), so the compiler
    # is asked which ones it had and they are put back with -idirafter: after
    # $(XSYSROOT)/usr/include, so a dependency's header always wins over any
    # copy of the same name shipped beside the libc headers.
    #
    # Port fix: the extraction keeps the paths as data -- only the *shape* of
    # the pipe is a shell's business -- and the backslashes are normalized by
    # make itself. Arm's Windows-host gcc prints `c:\arm-gnu-toolchain-9.2\
    # bin\..\lib\...`, and whether a shell sees one backslash, two, or none of
    # them depends on which make and which sh are in play (native make + the
    # kit's busybox sh mangled them, so gcc got `c:arm-gnu-toolchain-9.2bin..`
    # and `#include <math.h>` failed). $(subst) has no such variable. Forward
    # slashes are understood by every gcc.
    MUGEN_CROSS_CC_LINES := $(shell $(CC) $(MUGEN_CROSS_SYSROOT_FLAGS) -E -v -x c $(MUGEN_CROSS_NULL) 2>&1 | \
        sed -n '/^#include <...> search starts here:/,/^End of search list/p' | \
        sed -e '1d' -e '$$d' -e 's/^ \{1,\}//')
    MUGEN_CROSS_CC_INCLUDE := $(foreach d,$(subst \,/,$(MUGEN_CROSS_CC_LINES)),-idirafter $(d))
    # The host compiler serves pkg-config; only the .pc files must come from
    # the target tree. Both variables are replaced, not extended.
    #
    # PKG_CONFIG_SYSTEM_* matter as much as PKG_CONFIG_LIBDIR: pkgconf qualifies
    # system include/library paths for the *host* (/mingw64/include), so a
    # target .pc with -I${includedir} came back as the host's path and the build
    # compiled against the host's SDL2 headers. Pointing both variables at the
    # target tree is what makes ${includedir} resolve to the target.
    #
    # PKG_CONFIG_PATH has to be cleared as well, and it is not optional: pkgconf
    # searches it *in addition to* PKG_CONFIG_LIBDIR, so an inherited value wins
    # over the target tree. MSYS2 exports one by default
    # (C:\msys64\mingw64\lib\pkgconfig;...), and the mingw64 tree carries a
    # newer SDL2 under the same package name -- so the arm64 build silently
    # compiled against the *host's* SDL2 2.32 headers and died inside the
    # toolchain's arm_neon.h, thousands of lines away from the cause. The guard
    # below CPPFLAGS catches the same class of leak if it returns another way.
    # Port fix: the replacement was incomplete, not just the libdir.
    MUGEN_HOST_PKG_CONFIG ?= $(firstword $(foreach p,pkg-config pkgconf,$(if $(shell command -v $(p) 2>/dev/null && echo $(p)),$(p))))
    ifeq ($(filter default file environment undefined,$(origin PKG_CONFIG)),$(origin PKG_CONFIG))
      PKG_CONFIG := env PKG_CONFIG_LIBDIR='$(MUGEN_CROSS_LIB)/pkgconfig' \
                          PKG_CONFIG_PATH='' \
                          PKG_CONFIG_SYSTEM_INCLUDE_PATH='$(MUGEN_CROSS_INC)' \
                          PKG_CONFIG_SYSTEM_LIBRARY_PATH='$(MUGEN_CROSS_LIB)' \
                          PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 \
                          $(MUGEN_HOST_PKG_CONFIG)
    endif
  endif
endif
# Python is only needed for the optional icon (tools/extract_icon.py).
# Existing is not enough: on Windows 'python3' is usually the Microsoft Store
# alias, which "exists" and prints an advert instead of running, so every
# candidate is asked to run. Without MUGEN_GAME_DIR Python is never used, so
# its absence is not an error.
PYTHON3 ?= $(firstword $(foreach p,python3 python py,$(if $(shell $(p) -c "pass" 2>/dev/null && echo ok),$(p))))

DEBUG    ?=

MUGEN_GAME_DIR ?=

# SDL2 is the only backend. Reject stale SDL1 build commands explicitly.
ifneq ($(origin SDL_BACKEND),undefined)
  ifneq ($(SDL_BACKEND),2)
    $(error SDL2 is required; SDL_BACKEND=$(SDL_BACKEND) is no longer supported)
  endif
endif

# ------------------------------------------------------- platform detection
#
# uname -s decides the OS family; the compiler's own target triple decides the
# CPU, the ABI and -- on Windows -- which of win32/win64 is built. uname -m must
# NOT decide the architecture: inside MSYS it reports the host (x86_64 here)
# while the compiler targets i686-w64-mingw32, so trusting it would select
# 64-bit flags for a binary this project cannot link. The compiler wins on any
# disagreement.
#
# The triple is asked of the compiler, never assumed: -dumpmachine answers it
# directly, and -v prints the same triple on its "Target:" line, which is the
# only form a wrapper that swallows the first query prints. On Windows that
# means the kit on PATH decides the target with no variable to remember:
#
#   i686-w64-mingw32    (x86devkit)  -> win32, the reference build
#   x86_64-w64-mingw32  (w64devkit)  -> win64, the experimental 64-bit build
#
# Override with MUGEN_TARGET=<target> for cross-compiling, or to build the
# other Windows target with a compiler that is not first on PATH.

UNAME_S := $(shell uname -s 2>/dev/null)
TRIPLE  := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(strip $(TRIPLE)),)
  TRIPLE := $(shell $(CC) -v 2>&1 </dev/null | sed -n 's/^Target: *//p' | head -n 1)
endif

ifeq ($(origin MUGEN_TARGET),undefined)
  ifneq (,$(filter Windows_NT MINGW% MSYS% CYGWIN% NT,$(UNAME_S)))
    ifneq (,$(findstring i686-w64-mingw32,$(TRIPLE)))
      MUGEN_TARGET := win32
    else ifneq (,$(findstring x86_64-w64-mingw32,$(TRIPLE)))
      # The compiler on PATH is the 64-bit kit, so build the 64-bit target.
      # This used to be a hard error telling the caller to pass
      # MUGEN_TARGET=win64, which meant a plain `make` refused to run on a
      # w64devkit-only PATH even with every dependency present and readable.
      MUGEN_TARGET := win64
      MUGEN_TARGET_AUTO := 1
    else
      $(error unsupported Windows toolchain "$(TRIPLE)"; expected i686-w64-mingw32 or x86_64-w64-mingw32)
    endif
  else ifeq (Linux,$(UNAME_S))
    ifneq (,$(findstring x86_64-,$(TRIPLE)))
      MUGEN_TARGET := linux-x64
    else ifneq (,$(findstring aarch64-,$(TRIPLE)))
      MUGEN_TARGET := linux-arm64
    else ifneq (,$(findstring i686-,$(TRIPLE)))
      MUGEN_TARGET := linux-x86
    else
      $(error unsupported Linux triple "$(TRIPLE)"; supported: x86_64-, i686-, aarch64-)
    endif
  else ifeq (Darwin,$(UNAME_S))
    $(error macOS is not supported; the Makefile targets Windows and Linux)
  else
    $(error unsupported OS "$(UNAME_S)"; set MUGEN_TARGET explicitly to override)
  endif
endif

# Two families: win32 covers every Windows target (the 32-bit reference build
# and the experimental 64-bit one), linux covers every Linux target. The
# per-target differences (pointer width, kit, output name) are selected by
# MUGEN_TARGET below, never by the family: Windows targets still need the
# Win32 sys_libs, the .rc/manifest resources, -mwindows and static archives.
#
# win64 is experimental and never the reference build. It links a 64-bit
# static dependency kit (SDL2, zlib, libpng16) with winpthreads instead of
# pthreadVC2, and the engine has 64-bit-specific fixes in the BGM thread (native
# pthread storage, pointer packing). See docs/BUILD-MINGW64.md.
ifeq ($(MUGEN_TARGET),win32)
  MUGEN_FAMILY := win32
  MUGEN_PTRBITS := 32
  BUILD    ?= build/mingw-sdl2
  TARGET   := $(BUILD)/mugen-mingw-win32-sdl2.exe
else ifeq ($(MUGEN_TARGET),win64)
  MUGEN_FAMILY := win32
  MUGEN_PTRBITS := 64
  BUILD    ?= build/mingw64-sdl2
  TARGET   := $(BUILD)/mugen-mingw-win64-sdl2.exe
else
  MUGEN_FAMILY := linux
  MUGEN_PTRBITS := $(if $(findstring 64,$(MUGEN_TARGET)),64,32)
  # A cross build names its own directory: same triple, different toolchain and
  # dependency tree, so its objects must not be reused from the host build.
  ifneq ($(strip $(CROSS_COMPILE)),)
    MUGEN_CROSS_TAG := -$(patsubst %-,%,$(CROSS_COMPILE))
  endif
  BUILD    ?= build/$(MUGEN_TARGET)$(MUGEN_CROSS_TAG)-sdl2
  TARGET   := $(BUILD)/mugen-$(MUGEN_TARGET)$(MUGEN_CROSS_TAG)
endif

# A target picked from the compiler, rather than asked for, is announced: on a
# w64devkit-only PATH a plain `make` now produces the experimental 64-bit binary
# instead of refusing, and an unannounced swap of the reference build for it is
# the one surprise worth a line of output. Printed after the guards above, so a
# rejected configuration reports its error alone.
ifeq ($(MUGEN_TARGET_AUTO),1)
  $(info note: $(TRIPLE) is the 64-bit kit, so the win64 target is selected:)
  $(info       $(notdir $(TARGET)) -- the win32 reference build needs the i686)
  $(info       kit on PATH, e.g. PATH=/c/x86devkit/bin:$$PATH)
endif

# --------------------------------------------- Windows path spelling (helpers)
#
# A native Windows tool (windres, pkg-config.exe, or gcc.exe reading an -I) cannot
# open a POSIX /c/... path, and $(abspath) answers /c/... only when make itself
# runs under MSYS -- where cygpath is the thing that converts it. Asking cygpath
# unconditionally is wrong in the other direction: Git for Windows puts
# cygpath.exe on PATH even in a plain cmd.exe session, so a native build would
# spawn an MSYS binary for nothing and get the same answer back.
#
# MAKE_HOST is the make binary's own build triplet, so it names the shell world
# make is actually in: x86_64-pc-msys under MSYS2/Git Bash, x86_64-w64-mingw32
# for the kits' own make under cmd.exe (which is what build.bat uses). Off MSYS
# the conversion is empty and every caller falls back to the plain form.
ifeq ($(findstring msys,$(MAKE_HOST)),msys)
  MUGEN_PATH_TO_WIN = $(shell cygpath -m '$1' 2>/dev/null)
else
  MUGEN_PATH_TO_WIN =
endif

# -------------------------------------------------- the Windows dependency tree
#
# build.bat installs win32-sysroot/ or win64-sysroot/ under build/sysroots/, and
# a kit from skeeto's w64devkit ships no SDL, no libpng and no .pc file at all --
# so that tree is where the libraries come from. Pointing pkg-config at it here
# is what makes a plain `make` work after build.bat, with nothing exported.
#
# The paths are converted to Windows spelling on purpose: sh (what make runs
# recipes in) does not convert POSIX paths for a native pkg-config.exe, so a
# /c/... path silently matches nothing and the build dies with "pkg-config
# cannot find: SDL2_mixer ..." while the tree sits right there. $(abspath)
# answers C:/... under cmd but /c/... under MSYS, hence the conversion above;
# it is a no-op when make is the kits' own (MAKE_HOST), so a cmd build never
# looks for cygpath at all.
#
#   WINSYSROOT=<dir>   another tree          WINSYSROOT=   do not use one (the
#                                                        kit's own pkg-config
#                                                        setup, as before)
ifeq ($(MUGEN_FAMILY),win32)
  MUGEN_WIN_SYSROOT := $(if $(filter undefined,$(origin WINSYSROOT)),\
      $(firstword $(wildcard build/sysroots/$(MUGEN_TARGET)-sysroot)),$(WINSYSROOT))
  ifeq ($(wildcard $(MUGEN_WIN_SYSROOT)/usr/lib/pkgconfig/sdl2.pc),)
    ifneq ($(strip $(MUGEN_WIN_SYSROOT)),)
      $(warning WINSYSROOT=$(MUGEN_WIN_SYSROOT) has no usr/lib/pkgconfig/sdl2.pc; using the kit's own pkg-config setup)
    endif
    MUGEN_WIN_SYSROOT :=
  endif
  ifneq ($(strip $(MUGEN_WIN_SYSROOT)),)
    MUGEN_WIN_SYSROOT := $(abspath $(MUGEN_WIN_SYSROOT))
    MUGEN_WIN_SYSROOT_WIN := $(call MUGEN_PATH_TO_WIN,$(MUGEN_WIN_SYSROOT))
    ifeq ($(strip $(MUGEN_WIN_SYSROOT_WIN)),)
      MUGEN_WIN_SYSROOT_WIN := $(MUGEN_WIN_SYSROOT)
    endif
    MUGEN_WIN_INC := $(MUGEN_WIN_SYSROOT_WIN)/usr/include
    MUGEN_WIN_LIB := $(MUGEN_WIN_SYSROOT_WIN)/usr/lib
    ifeq ($(filter default file environment undefined,$(origin PKG_CONFIG)),$(origin PKG_CONFIG))
      PKG_CONFIG := env PKG_CONFIG_LIBDIR='$(MUGEN_WIN_LIB)/pkgconfig' \
                          PKG_CONFIG_PATH='' \
                          PKG_CONFIG_SYSTEM_INCLUDE_PATH='$(MUGEN_WIN_INC)' \
                          PKG_CONFIG_SYSTEM_LIBRARY_PATH='$(MUGEN_WIN_LIB)' \
                          PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 \
                          $(firstword $(foreach p,pkg-config pkgconf,$(if $(shell command -v $(p) 2>/dev/null && echo $(p)),$(p))))
    endif
  endif
endif

# --------------------------------------------------------------- GLES backend
#
# linux-arm64 defaults to the GLES 3.0 backend (MUGEN_GLES) instead of desktop
# OpenGL. The handhelds that target exists for (Mali-G31 class under ArkOS)
# expose OpenGL ES only: their libGL.so.1 is the GLX/glvnd stub, so gladLoadGL()
# resolves nothing against the EGL context SDL creates and Gl_Init rejects the
# context before the first frame. See PROBLEMS.md (P1) for the measurement.
#
# The reference builds keep desktop GL: win32/win64 need it for the log
# comparison against the original binary, and linux-x64 is the host reference.
# Override either way with MUGEN_GLES=1 or MUGEN_GLES=0.

ifeq ($(origin MUGEN_GLES),undefined)
  ifeq ($(MUGEN_TARGET),linux-arm64)
    MUGEN_GLES := 1
  else
    MUGEN_GLES := 0
  endif
endif

ifneq ($(filter-out 0 1,$(MUGEN_GLES)),)
  $(error MUGEN_GLES must be 0 or 1, not "$(MUGEN_GLES)")
endif

ifeq ($(MUGEN_GLES),1)
  ifneq ($(MUGEN_FAMILY),linux)
    $(error MUGEN_GLES=1 is only supported for Linux targets, not $(MUGEN_TARGET); \
            the Windows builds use the desktop renderer)
  endif
endif

# What check-toolchain expects `$(CC) -dumpmachine` to start with. Detection
# above already rejects a mismatched pair, but an explicit MUGEN_TARGET skips
# detection, and a wrong CC then silently builds for the wrong machine -- the
# one mistake worth catching. Empty means "no expectation" (unknown target).
ifeq ($(MUGEN_TARGET),win32)
  TRIPLE_RE := ^i686-w64-mingw32
else ifeq ($(MUGEN_TARGET),win64)
  TRIPLE_RE := ^x86_64-w64-mingw32
else ifeq ($(MUGEN_TARGET),linux-x64)
  TRIPLE_RE := ^x86_64-
else ifeq ($(MUGEN_TARGET),linux-arm64)
  TRIPLE_RE := ^aarch64-
else ifeq ($(MUGEN_TARGET),linux-x86)
  TRIPLE_RE := ^i686-
else
  TRIPLE_RE :=
endif

V        ?= 0

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

# --------------------------------------------------------------- directories

SRC_DIR   := src
TP_DIR    := third_party
SDK_DIR   := $(TP_DIR)/sdk
OBJ_DIR   := $(BUILD)/obj
LIB_DIR   := $(BUILD)/lib
# Records the configuration every object in $(OBJ_DIR) was compiled under.
# $(BUILD) already differs per backend, but the backend a directory belongs to
# has changed over time (SDL2 took over build/mingw when it became the default),
# so an existing tree can hold objects built for the other backend. Several
# SDL2-only sources (sdl2surface.c, sdl2keys.c) compile to empty objects under
# SDL 1.2 and include no header that would notice the switch, so make considers
# them up to date and the SDL2 link then fails on missing symbols. Touching this
# stamp when the configuration changes forces them to be recompiled.
CONFIG_STAMP := $(OBJ_DIR)/.config
# $(MUGEN_TARGET) already distinguishes win32/win64/linux-<arch>, so it covers
# the pointer width and architecture that ARCHFLAGS would add.
CONFIG_ID    := backend=sdl2-native-v1 target=$(MUGEN_TARGET) gles=$(MUGEN_GLES)
ifneq ($(strip $(MUGEN_WIN_SYSROOT)),)
# The dependency tree decides which headers and archives the objects were built
# against, so switching trees (or switching between a tree and an augmented kit)
# must recompile rather than reuse.
CONFIG_ID += winsysroot=$(MUGEN_WIN_SYSROOT)
endif
ifneq ($(strip $(MUGEN_CROSS)),)
# Port fix: the prefix is identical for Arm 9.2 and 15.3. Record the resolved
# compiler and version so selecting another kit cannot reuse the old binary.
CONFIG_ID += cc=$(shell command -v $(CC)) version=$(shell $(CC) -dumpfullversion -dumpversion) sysroot=$(MUGEN_CROSS_SYSROOT)
endif
ICON_DIR  := $(BUILD)/icon
# TARGET is set per platform in the detection block above.

# ------------------------------------------------------------- compile flags

# The engine was reconstructed from a 32-bit MSVC 2008 binary that runs the
# x87 FPU at 53-bit precision with /arch:IA32. -mfpmath=387 keeps float
# arithmetic on the x87 and -fexcess-precision=standard rounds on every
# assignment to a float, which is what the code in src/ assumes.
#
# On x86-64 the x87 still exists, so the same flags restore FLT_EVAL_METHOD == 2
# and the build stays bit-faithful to the reference (verified on GCC 11.4 with a
# _Static_assert on __FLT_EVAL_METHOD__). arm64 has no x87 at all, so there the
# flags are dropped and float results are allowed to drift.
#
# -fno-strict-aliasing: the reconstruction aliases through unions/pointers the
# way the original machine code did; do not let GCC exploit TBAA.
#
# -Wno-stringop-truncation: the reconstruction copies into fixed-size fields
# with strncpy/strncat exactly as the original did (the original relies on the
# zero-filled struct, not on NUL termination).
#
# -Wno-format-truncation: the same reason one level up. Where the original
# built a path or an error message with sprintf into a fixed buffer that the
# longest possible input cannot fit, the reconstruction uses snprintf and
# truncates, because the original's behaviour there is a stack or .data overrun
# (see the "Port fix" notes in src/select.c and src/storyboard.c). This warning
# fires on exactly those deliberate truncations; -Wformat-overflow, which fires
# on the unbounded sprintf, is still enabled and must stay clean.

ENGINE_WARN := -Wall -Wno-unused-parameter -Wno-unused-result \
               -Wno-stringop-truncation -Wno-format-truncation

ifeq ($(DEBUG),1)
  OPT     := -O0 -g3
  BUILD_DEFS := -DMUGEN_DEBUG
else
  OPT     := -O2 -g
  BUILD_DEFS :=
endif

LRS_DEFS := -DLIBRESAMPLE_STATIC

ifeq ($(MUGEN_FAMILY),win32)
  ifeq ($(MUGEN_TARGET),win64)
    ARCHFLAGS := -m64
  else
    ARCHFLAGS := -m32
  endif
  # The x87 still exists on x86-64, so the same FPU flags restore
  # FLT_EVAL_METHOD == 2 and keep float rounding as the original MSVC 2008
  # /arch:IA32 build did. Do not drop these on win64: the engine's float
  # arithmetic (src/ftol.h) depends on x87 excess precision.
  FPUFLAGS  := -mfpmath=387 -fexcess-precision=standard
  # PTW32_STATIC_LIB: third_party/sdk/include/pthread.h declares the pthread
  # functions __declspec(dllimport) unless this is set; we link pthreadVC2 as a
  # static archive. LIBRESAMPLE_STATIC does the same for libresample.h.
  #
  # win64 has no pthreadVC2: winpthreads ships with the 64-bit toolchain, so
  # -lpthread is added to SYS_LIBS instead and the 32-bit ABI SDK headers are
  # kept off the include path (see PLAT_CPPFLAGS below).
  ifeq ($(MUGEN_TARGET),win64)
    ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE $(LRS_DEFS)
    SYS_LIBS    := -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32 -lpthread
    PLAT_CPPFLAGS :=
  else
    ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE \
                   -DPTW32_STATIC_LIB $(LRS_DEFS)
    SYS_LIBS    := -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32
    # Win32 keeps the reference pthread/zlib declarations; no SDL1 headers.
    PLAT_CPPFLAGS := -I$(SDK_DIR)/include
  endif
  LINK_MODE   := -mwindows
  FORCE_INCLUDE :=
  PLAT_LDFLAGS  :=
else
  ARCHFLAGS :=
  ifeq ($(MUGEN_TARGET),linux-arm64)
    FPUFLAGS :=
  else
    FPUFLAGS := -mfpmath=387 -fexcess-precision=standard
  endif
  ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS $(LRS_DEFS)
  LINK_MODE   :=
  SYS_LIBS    := -lpthread
  # The reference Win32 pthread/zlib headers must not be on this path.
  PLAT_CPPFLAGS :=
  # compat_linux.h is force-included: several sources include <SDL.h> or
  # <libresample.h> before mugen.h, so __int64/__cdecl and the CRT renames must
  # be in place regardless of include order.
  FORCE_INCLUDE := -include $(SRC_DIR)/compat_linux.h
  # _GNU_SOURCE: strcasecmp/alloca and the POSIX extensions used here.
  PLAT_LDFLAGS  := -Wl,--gc-sections
  ENGINE_DEFS += -D_GNU_SOURCE
  # Cross build: the target's headers are the only headers. -nostdinc keeps the
  # host's /usr/include out of the picture (x86_64 SDL2, glibc's headers), so a
  # missing dependency is a compile error here instead of a host header being
  # silently mixed in; the toolchain's own include directories are then
  # re-added through the compiler itself. --sysroot is only passed when the
  # compiler was not configured with one (Arm's aarch64-none-linux-gnu carries
  # its glibc sysroot already; a bare cross gcc on a Linux host does not).
  ifneq ($(strip $(MUGEN_CROSS_SYSROOT)),)
    CROSS_CPPFLAGS := -nostdinc -isystem $(MUGEN_CROSS_INC) -isystem $(MUGEN_CROSS_INC)/SDL2 $(MUGEN_CROSS_SYSROOT_FLAGS) $(MUGEN_CROSS_CC_INCLUDE)
    # -rpath-link: the target's shared SDL2/SDL2_mixer are linked, not their
    # static archives, so those .so files pull in their own NEEDED libraries
    # (libasound, libpulse, libX11, libFLAC, ...). Those are only resolved by
    # the loader at run time, but ld still wants to see them when it is asked
    # for a real (non -shared) link, and --sysroot does not cover a dependency
    # tree that lives outside the compiler's own sysroot.
    MUGEN_CROSS_RPATH_LINK := $(MUGEN_CROSS_LIB)/rpath
    CROSS_LDFLAGS  := -L$(MUGEN_CROSS_LIB) $(MUGEN_CROSS_SYSROOT_FLAGS)
    ifneq ($(wildcard $(MUGEN_CROSS_RPATH_LINK)),)
      CROSS_LDFLAGS += -Wl,-rpath-link,$(MUGEN_CROSS_RPATH_LINK)
    endif
  endif
endif

# --- SDL2 dependencies ------------------------------------------------------
# pkg-config resolves the codec and font dependencies of the static kits.
# The engine loads PNG through libpng directly.
SDL2_PKGS := sdl2 SDL2_mixer SDL2_ttf
ifeq ($(MUGEN_FAMILY),win32)
  STATIC_PKGS := $(SDL2_PKGS) zlib libpng16
  # The engine owns WinMain and adds -mwindows via LINK_MODE.
  PKG_LIBS := $(filter-out -mwindows -lSDL2main,$(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null))
  SDK_LIB_DIR ?= $(shell $(PKG_CONFIG) --variable=libdir $(firstword $(STATIC_PKGS)) 2>/dev/null)
  CHECK_LIBS := SDL2 SDL2_mixer SDL2_ttf z mpg123
else
  STATIC_PKGS := $(SDL2_PKGS) zlib libpng
  # On GLES targets SDL2 loads EGL. Only the desktop renderer links GL/GLU.
  PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(MUGEN_CROSS_STATIC) $(STATIC_PKGS) 2>/dev/null) $(if $(filter 1,$(MUGEN_GLES)),,-lGL -lGLU) -ldl -lm
endif
PKG_CFLAGS := $(filter-out -Dmain=SDL_main,$(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null))
ifeq ($(origin PKG_CFLAGS),command line)
  # Retain SDL2's header path for callers overriding the other package flags.
  SDL_CFLAGS ?= $(filter-out -Dmain=SDL_main,$(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null))
endif
ENGINE_DEFS += -DSDL_MAIN_HANDLED

# --- GLES backend selection -------------------------------------------------
#
# MUGEN_GLES replaces the desktop renderer with the GLES 3.0 one:
#
#   - third_party/glad-es-3.0 (generated --api="gles2=3.0") becomes the GL
#     declaration source and the loader, instead of glad-2.1. The two share an
#     include guard upstream, so the include path must name exactly one of them;
#     putting glad-es first is what makes `#include <glad.h>` resolve there.
#   - src/glrender.c and src/gldetect.c leave the build. They are the desktop
#     renderer: fixed-function pipeline, GLSL 1.20 from the game's data/, and
#     the sub-pixel offset probe. src/gles_render.c defines the same Gl_ and
#     Gfx_Gl entry points against GLES, so compiling both would be a
#     duplicate-symbol link error -- and the desktop ones could not run anyway.
ifeq ($(MUGEN_GLES),1)
  ENGINE_DEFS += -DMUGEN_GLES
  GLAD_DIR := $(TP_DIR)/glad-es-3.0
  GLAD_SRC := $(GLAD_DIR)/glad.c
  ENGINE_EXCLUDE_SRCS := $(SRC_DIR)/glrender.c $(SRC_DIR)/gldetect.c
else
  GLAD_DIR := $(TP_DIR)/glad-2.1
  GLAD_SRC := $(GLAD_DIR)/glad.c
  ENGINE_EXCLUDE_SRCS :=
endif

# --- embedded shaders --------------------------------------------------------
#
# Every shader lives in src/shaders/ and is compiled in as a C string literal by
# tools/embed_shader.sh, for every target. Nothing is read from disk at run
# time. The generated headers sit under $(SHADER_HDR_DIR), and the name flattens
# '/' and '.' to '_', so gles/indexed.vert becomes g_shader_gles_indexed_vert and
# cannot collide with the model shaders or another dialect. See
# src/shaders/README.md for the dialects and the EB_DEFINES contract.
#
# Which sprite dialect is embedded follows the backend, exactly as the renderer
# source does: the GLES build compiles src/gles_render.c (gles/), the desktop
# build compiles src/glrender.c (desktop/, its fallback for the game's own
# data/*.vert). Vita picks its dialect up in port/psvita/CMakeLists.txt.
SHADER_HDR_DIR := $(BUILD)/shaders
ifeq ($(MUGEN_GLES),1)
  SPRITE_SHADER_NAMES := gles/indexed.vert gles/indexed.frag gles/rgba.vert gles/rgba.frag
else
  SPRITE_SHADER_NAMES := desktop/indexed.vert desktop/indexed.frag desktop/rgba.vert desktop/rgba.frag
endif
SPRITE_SHADER_HEADERS := $(addprefix $(SHADER_HDR_DIR)/,$(addsuffix .h,$(SPRITE_SHADER_NAMES)))
CPPFLAGS += -I$(SHADER_HDR_DIR)

# Desktop GL is the reference. GLES model support is opt-in until device qualification.
MUGEN_MODEL3D ?= $(if $(filter 1,$(MUGEN_GLES)),0,1)
ifneq ($(filter-out 0 1,$(MUGEN_MODEL3D)),)
  $(error MUGEN_MODEL3D must be 0 or 1)
endif
MODEL_SHADER_NAMES := model.vert model.frag shadow.vert shadow.frag env.vert env_panorama.frag env_filter.frag model_basic.vert model_basic.frag
MODEL_SHADER_HEADERS := $(addprefix $(SHADER_HDR_DIR)/,$(addsuffix .h,$(MODEL_SHADER_NAMES)))
ifeq ($(MUGEN_MODEL3D),1)
  ENGINE_DEFS += -DMUGEN_MODEL3D
  CPPFLAGS += -I$(TP_DIR)/cgltf-1.14 -I$(TP_DIR)/stb
  MODEL_TP_OBJS := $(OBJ_DIR)/model/cgltf.o $(OBJ_DIR)/model/stb_image.o
endif
CONFIG_ID += model3d=$(MUGEN_MODEL3D)

# SANITIZE=address (or undefined,thread) instruments the engine and links the
# runtime. Used while bringing up a new platform; not for shipping builds.
ifneq ($(SANITIZE),)
  OPT += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
  SYS_LIBS += -fsanitize=$(SANITIZE)
endif

# A cross build puts the target's include tree first and -- with -nostdinc --
# leaves only that plus the toolchain's own headers reachable. -isystem keeps
# the dependency headers out of the -Wall/-W3 warnings: they are not ours.
#
# That ordering is the reason a leaked .pc file has to be an error rather than a
# warning: cpp searches *every* -I before *any* -isystem, whatever order they
# appear in on the command line, so one host -I from pkg-config outranks the
# target tree above. Check what pkg-config actually answered instead of trusting
# the environment to hold the right answer (see PKG_CONFIG_PATH at the top).
#
# Pure make on purpose: a case/loop in $(shell ...) is not safe here, because
# make counts parentheses while it scans for the closing ')' of the function and
# a shell 'case' pattern ends in one -- it truncates the script silently. The
# tree is accepted under either spelling it can arrive in: make's $(abspath)
# answers /c/... from MSYS while pkg-config answers C:/... for the same file, and
# comparing only one of them reports a correct tree as a leak. MUGEN_PATH_TO_WIN
# bridges the two; off MSYS it is empty and the plain form is used.
ifneq ($(strip $(MUGEN_CROSS_SYSROOT)),)
  MUGEN_CROSS_SYSROOT_WIN := $(call MUGEN_PATH_TO_WIN,$(MUGEN_CROSS_SYSROOT))
  MUGEN_CROSS_INC_OK := $(MUGEN_CROSS_SYSROOT)% \
                        $(if $(MUGEN_CROSS_SYSROOT_WIN),$(MUGEN_CROSS_SYSROOT_WIN)%)
  MUGEN_CROSS_FOREIGN_INC := $(filter-out $(MUGEN_CROSS_INC_OK), \
      $(patsubst -I%,%,$(filter -I%,$(PKG_CFLAGS) $(SDL_CFLAGS))))
  ifneq ($(strip $(MUGEN_CROSS_FOREIGN_INC)),)
    $(error pkg-config returned include paths outside the target tree: $(MUGEN_CROSS_FOREIGN_INC) \
            every dependency header must come from $(MUGEN_CROSS_SYSROOT); \
            a host .pc file was picked up (check PKG_CONFIG_PATH / PKG_CONFIG_LIBDIR, \
            or rebuild the tree with port\arm64\build.bat --sysroot))
  endif
endif
CPPFLAGS += $(CROSS_CPPFLAGS) \
            -I$(SRC_DIR) \
            -I$(TP_DIR)/libresample/include \
            $(PLAT_CPPFLAGS) \
            -I$(TP_DIR)/lua-5.1.4/src \
            -I$(GLAD_DIR) \
            -I$(TP_DIR)/minizip-0.15 \
            -I$(TP_DIR)/lz4 \
            $(PKG_CFLAGS) $(SDL_CFLAGS)

# mugen-unleashed: local WebM backgrounds in the Linux GLES build.
MUGEN_VIDEO_BG ?= $(if $(and $(filter linux-arm64,$(MUGEN_TARGET)),$(filter 1,$(MUGEN_GLES))),1,0)
ifeq ($(MUGEN_VIDEO_BG),1)
  ifneq ($(MUGEN_GLES),1)
    $(error MUGEN_VIDEO_BG=1 requires MUGEN_GLES=1)
  endif
  ifeq ($(filter linux-%,$(MUGEN_TARGET)),)
    $(error MUGEN_VIDEO_BG=1 requires Linux)
  endif
  FFMPEG_SRC := $(abspath $(TP_DIR)/ffmpeg-7.1.5)
  FFMPEG_BUILD := $(abspath $(BUILD)/ffmpeg)
  FFMPEG_STAMP := $(FFMPEG_BUILD)/built
  FFMPEG_LIBS := $(foreach lib,avformat avcodec swscale avutil,$(FFMPEG_BUILD)/lib$(lib)/lib$(lib).a)
  ifneq ($(strip $(CROSS_COMPILE)),)
    ifneq ($(MUGEN_TARGET),linux-arm64)
      $(error Cross-building video backgrounds requires the ARM64 toolchain)
    endif
    FFMPEG_ARCH_FLAGS := --enable-cross-compile --arch=aarch64 --target-os=linux --cross-prefix="$(CROSS_COMPILE)"
  else ifneq ($(filter linux-x64 linux-x86,$(MUGEN_TARGET)),)
    FFMPEG_ARCH_FLAGS := --disable-x86asm
  endif
  ENGINE_DEFS += -DMUGEN_VIDEO_BG
  CPPFLAGS += -I$(FFMPEG_BUILD) -I$(FFMPEG_SRC)
endif
CONFIG_ID += video_bg=$(MUGEN_VIDEO_BG)

# FORCE_INCLUDE stays out of COMMONFLAGS: it supplies Windows type names
# (BOOL, WORD, ...) that clash with the same names in the X11 headers a
# third-party GL library can pull in. Only the engine needs it.
COMMONFLAGS := $(ARCHFLAGS) -std=gnu11 $(FPUFLAGS) \
               -fno-strict-aliasing -MMD -MP $(OPT)

ENGINE_CFLAGS := $(COMMONFLAGS) $(FORCE_INCLUDE) $(ENGINE_WARN) $(ENGINE_DEFS) $(BUILD_DEFS)
# Third-party warning policy is separate from engine diagnostics.
TP_CFLAGS     := $(COMMONFLAGS) -w $(ENGINE_DEFS)

# --- the window icon, only meaningful on Windows ------------------------------
#
# The artwork lives in res/icons/ as one single-size .ico per size: 1.ico is
# 64x64, 2.ico 48, 8.ico 32, 3.ico 24, 4.ico 16, all 32-bit with an alpha
# channel (5, 6, 7 and 9 are the same drawing as 8-bit palette images). One
# .ico holding several sizes is what Windows wants: it picks 16 for the title
# bar, 32 for the task bar, 48 for Alt+Tab and 64+ for the large views, and a
# single-size file is scaled -- blurry -- for every other one. tools/iconpack.py
# merges them; with no Python the first icon is copied as-is, which still builds
# and still shows the right picture.
ifeq ($(MUGEN_FAMILY),win32)
  ICON_ICO := $(ICON_DIR)/mugen.ico
  ENGINE_CFLAGS += -DMUGEN_HAS_ICON -I$(ICON_DIR)
  ifneq ($(MUGEN_GAME_DIR),)
    ifneq ($(wildcard $(MUGEN_GAME_DIR)/mugen.exe),)
      ifeq ($(strip $(PYTHON3)),)
        $(error MUGEN_GAME_DIR is set but no working Python was found; set PYTHON3=...)
      endif
      ICON_MODE := extract
    else
      $(warning MUGEN_GAME_DIR is set but $(MUGEN_GAME_DIR)/mugen.exe is not there; using res/icons)
    endif
  endif
  ifeq ($(ICON_MODE),)
    MUGEN_ICONS ?= res/icons/1.ico res/icons/2.ico res/icons/8.ico res/icons/3.ico res/icons/4.ico
    ifeq ($(strip $(PYTHON3)),)
      ICON_MODE := copy
    else
      ICON_MODE := merge
    endif
  endif
endif

# ------------------------------------------------------------------- sources

# The engine, one file per module of the original (see docs/ANALYSIS.md).
# src/sdl2audio.c is not an original module: it supplies Sound_QueryMixerSpec,
# for the engine's chunk-size contract, implemented over stock SDL2_mixer.
#
# ENGINE_EXCLUDE_SRCS removes the desktop renderer under MUGEN_GLES (see the
# GLES backend block above); it is empty for every other build.
ENGINE_SRCS := $(filter-out $(ENGINE_EXCLUDE_SRCS),$(wildcard $(SRC_DIR)/*.c))
ENGINE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/src/%.o,$(ENGINE_SRCS))

# Third-party static libraries built from the vendored sources.
LUA_SRCS := $(wildcard $(TP_DIR)/lua-5.1.4/src/*.c)

# libresample 0.1.3, vendored and ported to multi-channel to match the ABI of
# the shipped libresample.dll (see third_party/libresample/VENDORED.md).
LRS_SRCS := $(wildcard $(TP_DIR)/libresample/src/*.c)

LUA_OBJS := $(patsubst $(TP_DIR)/lua-5.1.4/src/%.c,$(OBJ_DIR)/lua/%.o,$(LUA_SRCS))
LRS_OBJS := $(patsubst $(TP_DIR)/libresample/src/%.c,$(OBJ_DIR)/libresample/%.o,$(LRS_SRCS))
GLAD_OBJ := $(OBJ_DIR)/glad/glad.o
MINIZIP_OBJ := $(OBJ_DIR)/minizip/unzip.o
LZ4_OBJ := $(OBJ_DIR)/lz4/lz4.o

LUA_LIB := $(LIB_DIR)/liblua.a
LRS_LIB := $(LIB_DIR)/libresample.a
TP_LIBS := $(LUA_LIB) $(LRS_LIB) $(GLAD_OBJ) $(MINIZIP_OBJ) $(LZ4_OBJ) $(MODEL_TP_OBJS)

# Resources: the error dialog (200) and version info from mugen.rc, plus an
# assembly manifest (the original embeds one the same way, as RT_MANIFEST 1).
# windres takes a single .rc, so the manifest is appended to a copy of mugen.rc
# instead of being compiled separately -- keeping it one .rc is what makes this
# build independent of cvtres /merge.
#
# The manifest is NOT src/mugen.manifest: that one declares a dependency on the
# private assembly Elecbyte.MUGEN.libs, which resolves the original's DLLs. The
# loader enforces it before any code runs, so this statically linked build
# refused to start without a folder of DLLs it never loads. See
# src/mugen-static.manifest.
ifeq ($(MUGEN_FAMILY),win32)
RC_OBJ  := $(OBJ_DIR)/res/mugen_all.res.o
ALL_RC  := $(OBJ_DIR)/res/mugen_all.rc
else
# mugen.rc is a PE resource script (dialog 200, version info, manifest). An ELF
# executable has no equivalent, so there is nothing to compile.
RC_OBJ  :=
ALL_RC  :=
endif

# --------------------------------------------------------------------- rules

.PHONY: all clean distclean check-toolchain check-libs check-pkgconfig help install perf \
        validate validate-run sffcli check-memprof-buckets check-explods-count
.DELETE_ON_ERROR:

# check-pkgconfig runs first so a missing .pc file is reported as such instead
# of as the several hundred undefined Mix_*/TTF_*/SDL_* symbols it causes at
# link time. Supplying PKG_LIBS yourself on the command line skips it.
ALL_DEPS := $(TARGET)
ifneq ($(origin PKG_LIBS),command line)
  ALL_DEPS := check-pkgconfig $(TARGET)
endif

all: $(ALL_DEPS)

# Static link. -Wl,--gc-sections drops the archive members nothing references;
# without it a static link of SDL_mixer pulls in every codec whether the game
# can use it or not.
# --exclude-all-symbols: SDL's main library asks the linker to export every
# symbol, which for a static link would export the entire engine (~600 names)
# and a few hundred KB of names with it. The engine exports nothing.
$(TARGET): $(ENGINE_OBJS) $(RC_OBJ) $(TP_LIBS) $(FFMPEG_STAMP)
	@echo "  LD      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ARCHFLAGS) -o $@ $(ENGINE_OBJS) $(RC_OBJ) \
	    $(PLAT_LDFLAGS) $(CROSS_LDFLAGS) \
	    -L$(LIB_DIR) -llua -lresample \
	    $(GLAD_OBJ) $(MINIZIP_OBJ) $(LZ4_OBJ) $(MODEL_TP_OBJS) \
	    $(FFMPEG_LIBS) $(if $(FFMPEG_LIBS),-lm) $(PKG_LIBS) $(SYS_LIBS) \
	    $(LINK_MODE)

# --- the engine -------------------------------------------------------------

# Writes $(CONFIG_STAMP) whenever $(CONFIG_ID) changes. FORCE forces the recipe
# to run on every build; the file is rewritten only when the content differs, so
# an unchanged configuration leaves every object's timestamp alone and nothing is
# recompiled. This is what makes changing the target/architecture in an
# existing build directory safe.
#
# Every compiled artefact depends on it: the engine sources, Lua, libresample,
# glad and minizip, and the windres output. Leaving any of them out meant a
# BUILD= directory reused from win32 to win64 kept back the old objects (glad and
# minizip are architecture-dependent like everything else) and the link failed on
# incompatible objects instead of rebuilding.
FORCE:
.PHONY: FORCE

$(CONFIG_STAMP): FORCE
	$(Q)mkdir -p $(dir $@)
	$(Q)printf '%s\n' '$(CONFIG_ID)' > $@.tmp
	$(Q)cmp -s $@.tmp $@ 2>/dev/null && rm -f $@.tmp || mv -f $@.tmp $@

# Keep a checked-in header for the CMake ports as well as the Make build.
res/lua_script_zip.h: res/lua_script.zip tools/embed_lua_scripts.py
	$(Q)$(PYTHON3) tools/embed_lua_scripts.py

$(OBJ_DIR)/src/luascripts.o: res/lua_script_zip.h

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -c $< -o $@

ifeq ($(MUGEN_VIDEO_BG),1)
$(OBJ_DIR)/src/video_bg.o: $(FFMPEG_STAMP)

# FFmpeg's configure probes create temporary symlinks and compile tests via
# mktemp. With TMPDIR unset that is the Windows user temp directory, which this
# project must not write to (AGENTS.md, "keep temporary files inside the
# project") -- and on this host the probe symlink is refused outright, printing
# "ln: .../Temp/name_XXXX: Permission denied" on every configure. Pointing
# TMPDIR at $(BUILD)/tmp keeps them inside the repository and silences the
# noise. It is created here because configure runs before anything else does.
FFMPEG_TMP := $(abspath $(BUILD)/tmp/ffmpeg)

$(FFMPEG_STAMP): $(CONFIG_STAMP) $(wildcard $(FFMPEG_SRC)/configure) Makefile
	$(Q)test -f "$(FFMPEG_SRC)/configure" || { echo "Run git submodule update --init third_party/ffmpeg-7.1.5"; exit 1; }
	@echo "  BUILD   FFmpeg (WebM)"
	$(Q)rm -rf "$(FFMPEG_BUILD)"
	$(Q)mkdir -p "$(FFMPEG_BUILD)" "$(FFMPEG_TMP)"
	$(Q)cd "$(FFMPEG_BUILD)" && TMPDIR="$(FFMPEG_TMP)" TMP="$(FFMPEG_TMP)" TEMP="$(FFMPEG_TMP)" \
	    "$(FFMPEG_SRC)/configure" $(FFMPEG_ARCH_FLAGS) \
	    --cc="$(CC)" --extra-cflags="$(ARCHFLAGS)" --disable-everything \
	    --disable-autodetect --disable-programs --disable-doc --disable-network \
	    --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc \
	    --disable-shared --enable-static --disable-debug \
	    --enable-protocol=file --enable-demuxer=matroska --enable-decoder=vp8,vp9 \
	    --enable-parser=vp8,vp9
	$(Q)$(MAKE) -C "$(FFMPEG_BUILD)" TMPDIR="$(FFMPEG_TMP)" TMP="$(FFMPEG_TMP)" TEMP="$(FFMPEG_TMP)"
	$(Q)touch "$@"
endif

# Every generated shader header comes from its source file. The variable name is
# the path with '/' and '.' flattened to '_' (g_shader_gles_indexed_vert), which
# is what keeps the dialects and the model shaders from sharing a symbol.
$(SHADER_HDR_DIR)/%.h: $(SRC_DIR)/shaders/% tools/embed_shader.sh
	@echo "  SHADER  $<"
	$(Q)sh tools/embed_shader.sh "$<" "$@" "g_shader_$(subst /,_,$(subst .,_,$*))"
	$(Q)touch "$@" # Acknowledge newer prerequisites even when shader bytes agree.

# The renderers that include the generated headers must rebuild when one changes.
$(OBJ_DIR)/src/gles_render.o: $(SRC_DIR)/gles_render.c $(SPRITE_SHADER_HEADERS)
$(OBJ_DIR)/src/glrender.o: $(SRC_DIR)/glrender.c $(SPRITE_SHADER_HEADERS)

ifeq ($(MUGEN_MODEL3D),1)
$(OBJ_DIR)/src/model.o: $(SRC_DIR)/model.c $(MODEL_SHADER_HEADERS)

$(OBJ_DIR)/model/cgltf.o: $(TP_DIR)/cgltf-1.14/cgltf.c $(CONFIG_STAMP)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -c $< -o $@

$(OBJ_DIR)/model/stb_image.o: $(TP_DIR)/stb/stb_image.c $(CONFIG_STAMP)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -c $< -o $@
endif

# --- Lua 5.1.4 --------------------------------------------------------------

# Port fix: select Lua's existing POSIX implementation on Linux. The default
# tmpnam implementation emits a linker warning; no readline dependency needed.
LUA_PLATFORM_DEFS := $(if $(filter linux,$(MUGEN_FAMILY)),-DLUA_USE_MKSTEMP)

$(OBJ_DIR)/lua/%.o: $(TP_DIR)/lua-5.1.4/src/%.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) $(LUA_PLATFORM_DEFS) -I$(TP_DIR)/lua-5.1.4/src -c $< -o $@

$(LUA_LIB): $(LUA_OBJS)
	@echo "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

# --- libresample 0.1.3 (vendored, multi-channel) ----------------------------
# -DLIBRESAMPLE_STATIC turns off __declspec(dllimport) in the header.

$(OBJ_DIR)/libresample/%.o: $(TP_DIR)/libresample/src/%.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -DLIBRESAMPLE_STATIC \
	    -I$(TP_DIR)/libresample/include -I$(TP_DIR)/libresample/src -c $< -o $@

$(LRS_LIB): $(LRS_OBJS)
	@echo "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

# --- glad (2.1 desktop / es-3.0 GLES) --------------------------------------
# glad-2.1 is generated for this engine: GL 2.1 compatibility profile plus
# ARB_fragment_program, ARB_texture_non_power_of_two, EXT_blend_logic_op and
# EXT_framebuffer_object. glad-es-3.0 is the same generator with
# --api="gles2=3.0". Both are one self-contained .c with no import library;
# MUGEN_GLES picks which one, and the two never appear in one binary (they
# share an include guard).

$(GLAD_OBJ): $(GLAD_SRC) $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(GLAD_DIR) -c $< -o $@

# --- minizip (unzip 0.15) ---------------------------------------------------

$(MINIZIP_OBJ): $(TP_DIR)/minizip-0.15/unzip.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/minizip-0.15 -I$(SDK_DIR)/include -c $< -o $@

# --- lz4 (v1.10.0) ----------------------------------------------------------
# mugen-unleashed addition: the LZ4 block codec behind sprite format 13.
$(LZ4_OBJ): $(TP_DIR)/lz4/lz4.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/lz4 -c $< -o $@

# --- resources --------------------------------------------------------------

ifeq ($(ICON_MODE),extract)
$(ICON_ICO): $(MUGEN_GAME_DIR)/mugen.exe tools/extract_icon.py
	@echo "  ICON    $@  (from $(MUGEN_GAME_DIR)/mugen.exe)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(PYTHON3) tools/extract_icon.py "$(MUGEN_GAME_DIR)/mugen.exe" $@
else ifeq ($(ICON_MODE),merge)
$(ICON_ICO): $(MUGEN_ICONS) tools/iconpack.py
	@echo "  ICON    $@  (merging $(notdir $(MUGEN_ICONS)))"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(PYTHON3) tools/iconpack.py ico $@ $(MUGEN_ICONS)
else ifeq ($(ICON_MODE),copy)
$(ICON_ICO): $(word 1,$(MUGEN_ICONS))
	@echo "  ICON    $@  (copy of $(notdir $(word 1,$(MUGEN_ICONS))); no python3, so single size)"
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $@
else
$(ICON_ICO):
	@echo "  ICON    no icon source selected; nothing to do"
endif

MANIFEST := $(SRC_DIR)/mugen-static.manifest

# The .rc names the manifest for windres' own preprocessor, and windres is a
# native Windows binary: it cannot open a POSIX /c/... path (the failure is
# "can't open file `/c/.../mugen-static.manifest'"). $(CURDIR) answers C:/...
# under cmd but /c/... under MSYS, so convert it the same way the pkg-config
# paths above are converted; off MSYS the conversion is empty and abspath stands.
MUGEN_RC_MANIFEST := $(call MUGEN_PATH_TO_WIN,$(CURDIR)/$(MANIFEST))
ifeq ($(strip $(MUGEN_RC_MANIFEST)),)
  MUGEN_RC_MANIFEST := $(abspath $(MANIFEST))
endif

ifeq ($(ICON_ICO),)
$(ALL_RC): $(SRC_DIR)/mugen.rc $(MANIFEST)
else
$(ALL_RC): $(SRC_DIR)/mugen.rc $(MANIFEST) $(ICON_ICO)
endif
	@echo "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)cat $(SRC_DIR)/mugen.rc > $@
	$(Q)printf '\n/* generated: the assembly manifest, RT_MANIFEST 1 */\n1 24 "%s"\n' \
	    "$(MUGEN_RC_MANIFEST)" >> $@

# windres runs its own preprocessor, so it needs the define on its command line
# too: the .rc includes icon 101 only under MUGEN_HAS_ICON, and passing it to the
# C compiler alone left the resource out entirely (no RT_GROUP_ICON, no RT_ICON,
# and an executable whose icon silently stayed the default).
RC_DEFS :=
ifneq ($(ICON_ICO),)
RC_DEFS += -DMUGEN_HAS_ICON
endif

$(RC_OBJ): $(ALL_RC) $(CONFIG_STAMP)
	@echo "  WINDRES $<"
	$(Q)$(WINDRES) $(RC_DEFS) -I$(SRC_DIR) -I$(ICON_DIR) -i $< -o $@

# --- install ----------------------------------------------------------------
# Strips the executable and copies it into the game directory, where it stays.
# A directory with no data/mugen.cfg gets the project's default M.U.G.E.N assets
# first (tools/fetch_game_data.sh), so `make install` on a fresh clone leaves
# something that runs; the fetch is skipped when the tree is already there and
# a failed download only warns.
# install/ is the shipped M.U.G.E.N 1.1b1 tree (git-ignored). The installed
# name follows $(TARGET), so it is never the original's mugen.exe and never a
# Windows name on Linux: mugen-mingw-*.exe on Windows, mugen-linux-<arch> on
# Linux (an ELF binary called .exe was the old behaviour here). The name marks
# the backend, so installing one backend never clobbers the other.
#
# data/mugen.cfg is res/mugen.cfg, the project's documented default (see its
# header for what every key does). It is installed when the tree has no config
# -- i.e. on a fresh clone, right after the fetch -- and NOT over one that is
# already there, because that file is the player's: the options screen rewrites
# [Options], the volumes, the joystick type and the key groups in place, so
# replacing it on every install would discard every setting the player saved.
# MUGEN_CFG_FORCE=1 replaces it anyway, and MUGEN_CFG=<path> installs a
# different file.

INSTALL_DIR  ?= install
INSTALL_NAME ?= $(notdir $(TARGET))
STRIPPED     := $(BUILD)/$(basename $(INSTALL_NAME))-stripped$(suffix $(INSTALL_NAME))

# The default configuration. Absolute, so the recipe works from any directory
# and the existence check names the same file the copy reads.
MUGEN_CFG       ?= $(CURDIR)/res/mugen.cfg
MUGEN_CFG_FORCE ?= 0

$(STRIPPED): $(TARGET)
	@echo "  STRIP   $@"
	$(Q)$(STRIP) -o $@ $(TARGET)

install: $(STRIPPED)
	@fresh=0; \
	if [ ! -f "$(INSTALL_DIR)/data/mugen.cfg" ]; then \
	    fresh=1; \
	    echo "  DATA    $(INSTALL_DIR)/ (default M.U.G.E.N assets)"; \
	    sh tools/fetch_game_data.sh --dest "$(INSTALL_DIR)" || \
	        echo "  WARN    no game data: the binary will exit at start"; \
	fi; \
	if [ ! -f "$(MUGEN_CFG)" ]; then \
	    echo "  ERROR   MUGEN_CFG=$(MUGEN_CFG) does not exist"; exit 1; \
	fi; \
	mkdir -p $(INSTALL_DIR)/data; \
	if [ "$$fresh" = 1 ] || [ "$(MUGEN_CFG_FORCE)" != "0" ]; then \
	    echo "  CFG     $(INSTALL_DIR)/data/mugen.cfg <- $(notdir $(MUGEN_CFG))"; \
	    cp -f "$(MUGEN_CFG)" "$(INSTALL_DIR)/data/mugen.cfg"; \
	else \
	    echo "  CFG     $(INSTALL_DIR)/data/mugen.cfg kept (MUGEN_CFG_FORCE=1 to replace)"; \
	fi
	@echo "  INSTALL $(INSTALL_DIR)/$(INSTALL_NAME)"
	$(Q)cp $(STRIPPED) $(INSTALL_DIR)/$(INSTALL_NAME)
ifeq ($(MUGEN_TARGET),linux-arm64)
	$(Q)mkdir -p $(INSTALL_DIR)/data
	$(Q)cp port/arm64/mugen.arm64.cfg $(INSTALL_DIR)/data/mugen.arm64.cfg
endif
	@echo "  done: run it from $(INSTALL_DIR)"
ifeq ($(MUGEN_FAMILY),win32)
	@echo "        (no Elecbyte.MUGEN.libs needed)"
endif

# --- performance runs -------------------------------------------------------
# Runs the installed binary over a fixed scenario once per renderer and
# summarises both logs with tools/measure_perf.sh. The output shape and what the
# numbers mean are in docs/PERFORMANCE.md.
#
#   make perf                      the default scenario, 20 s per renderer
#   make perf PERF_SECONDS=40      longer, steadier samples
#   make perf PERF_ARGS="-p1 kfm -p2 kfm"   a specific match
#
# Needs [Debug] ShowFPS = 1 in mugen.cfg for the per-second lines; without it
# only the exit and per-match statistics exist and the tool says so.
PERF_SECONDS ?= 20
PERF_ARGS    ?= -p1 kfm -p2 kfm
PERF_DIR     ?= build/perf

perf: install
	@PERF_SECONDS='$(PERF_SECONDS)' PERF_DIR='$(PERF_DIR)' \
	  sh tools/run_perf.sh '$(INSTALL_DIR)' '$(INSTALL_NAME)' $(PERF_ARGS)

.PHONY: perf

# --- asset path validation --------------------------------------------------
# Builds tools/validate: it checks that a game pack's asset paths resolve on
# this filesystem, which matters because Windows
# compares filenames case-insensitively and Linux does not. Windows-made packs
# routinely carry "chars\KFM\KFM.def" or "states\constants.cns" and fail on a
# Linux target with only "Error reading character file" to show for it.
#
# The tool is standalone: no engine objects, no SDL, no Lua, no display. It is
# host-native because that is where you run it -- on the machine that holds the
# pack -- but it cross-compiles to the handheld with the same CROSS_COMPILE the
# engine uses, so the device can check its own media:
#
#   make validate                     build build/<host>/validate
#   make validate-run VALIDATE_DIR=...  build, then run it over a game directory
#   make validate MUGEN_TARGET=linux-arm64 CROSS_COMPILE=aarch64-none-linux-gnu- \
#        XSYSROOT=build/sysroots/arm64-sysroot   the same tool for the device
#
# The tool itself does not use the sysroot or pkg-config; those variables are
# only carried so the command line matches the port's build.bat. The engine's
# COMMONFLAGS are deliberately NOT used: they carry -MMD into $(OBJ_DIR) and
# engine defines this program must not inherit.
VALIDATE_SRC := tools/validate.c
VALIDATE_BIN := $(BUILD)/validate$(if $(filter win32,$(MUGEN_FAMILY)),.exe)
VALIDATE_DIR ?=
VALIDATE_ARGS ?=
# -D_GNU_SOURCE for localtime_r on glibc; the tool is otherwise plain C.
VALIDATE_CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE $(VALIDATE_CFLAGS_EXTRA)

$(VALIDATE_BIN): $(VALIDATE_SRC)
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(VALIDATE_CFLAGS) $< -o $@
	@echo "validate: $@"

validate: $(VALIDATE_BIN)

validate-run: $(VALIDATE_BIN)
	@test -n '$(VALIDATE_DIR)' || { \
	  echo "error: VALIDATE_DIR is required, eg. make validate-run VALIDATE_DIR=install"; \
	  exit 2; \
	}
	@cd '$(VALIDATE_DIR)' && '$(abspath $(VALIDATE_BIN))' $(VALIDATE_ARGS)

.PHONY: validate validate-run

# --- Standalone SFF inspection, PNG export and repacking ----------------------
SFFCLI_BIN := $(BUILD)/sffcli$(if $(filter win32,$(MUGEN_FAMILY)),.exe)

$(SFFCLI_BIN): tools/sffcli.c third_party/lz4/lz4.c Makefile
	@echo "  CC      $@"
	$(Q)mkdir -p "$(dir $@)"
	$(Q)$(CC) $(ARCHFLAGS) -std=c11 $(OPT) -Wall -Wextra -Ithird_party/lz4 $(SFFCLI_CFLAGS_EXTRA) -o "$@" tools/sffcli.c third_party/lz4/lz4.c

sffcli: $(SFFCLI_BIN)
.PHONY: sffcli

# Production-loader probe used by tools/test_sff3.py (standalone CLI stays independent).
SFF3_TEST_DIR := $(BUILD)/sff3-test
SFF3_PROBE := $(SFF3_TEST_DIR)/probe$(if $(filter win32,$(MUGEN_FAMILY)),.exe)
$(SFF3_TEST_DIR)/main.o: src/main.c $(CONFIG_STAMP)
	@mkdir -p $(SFF3_TEST_DIR)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -DMUGEN_NO_ENTRY -MMD -MP -c $< -o $@
$(SFF3_TEST_DIR)/probe.o: tools/test_sff3_engine.c $(CONFIG_STAMP)
	@mkdir -p $(SFF3_TEST_DIR)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@
-include $(SFF3_TEST_DIR)/main.d $(SFF3_TEST_DIR)/probe.d
SFF3_PROBE_OBJS := $(filter-out $(OBJ_DIR)/src/main.o $(OBJ_DIR)/src/platform_main_win32.o,$(ENGINE_OBJS)) $(SFF3_TEST_DIR)/main.o $(SFF3_TEST_DIR)/probe.o
$(SFF3_PROBE): $(SFF3_PROBE_OBJS) $(TP_LIBS)
	$(Q)$(CC) $(ARCHFLAGS) -o $@ $(SFF3_PROBE_OBJS) $(PLAT_LDFLAGS) $(CROSS_LDFLAGS) -L$(LIB_DIR) -llua -lresample $(GLAD_OBJ) $(MINIZIP_OBJ) $(LZ4_OBJ) $(MODEL_TP_OBJS) $(PKG_LIBS) $(SYS_LIBS) $(LINK_MODE) $(if $(filter win32,$(MUGEN_FAMILY)),-mconsole)
sff3-probe: $(SFF3_PROBE)
.PHONY: sff3-probe

# --- SFF codec benchmark (build/tmp copy, not a shipped tool) ----------------
# Measures load/decode time and pool residency of an archive through the real
# loader and codecs. Source lives under build/tmp so it is never committed.
BENCH_SFF := $(BUILD)/sff3-test/bench_sff$(if $(filter win32,$(MUGEN_FAMILY)),.exe)
$(BENCH_SFF): build/tmp/bench_sff.c $(CONFIG_STAMP) $(SFF3_TEST_DIR)/main.o
	@echo "  CC      $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -c build/tmp/bench_sff.c -o $(SFF3_TEST_DIR)/bench_sff.o
	$(Q)$(CC) $(ARCHFLAGS) -o $@ $(filter-out $(OBJ_DIR)/src/main.o $(OBJ_DIR)/src/platform_main_win32.o,$(ENGINE_OBJS)) $(SFF3_TEST_DIR)/main.o $(SFF3_TEST_DIR)/bench_sff.o $(PLAT_LDFLAGS) $(CROSS_LDFLAGS) -L$(LIB_DIR) -llua -lresample $(GLAD_OBJ) $(MINIZIP_OBJ) $(LZ4_OBJ) $(MODEL_TP_OBJS) $(PKG_LIBS) $(SYS_LIBS) $(LINK_MODE) $(if $(filter win32,$(MUGEN_FAMILY)),-mconsole)

bench-sff: $(BENCH_SFF)
.PHONY: bench-sff

# --- memprof side-table benchmark -------------------------------------------
# Times src/memprof.c's pointer side table at several bucket counts, using
# tools/test_memprof_buckets.c. Read its header first: the interesting metric is
# `drain` (frees at full occupancy, i.e. the chain walk), NOT `churn` (an
# alloc+free pair lands on the chain head and is O(1) either way).
#
# Why this exists: an on-device profile put Memprof_FindEntryLocked at 12.2% of
# all samples on a heavy pack. Chasing that with 7 paired fps runs could not
# resolve it (t=1.85), and the reason turned out to be that every one of those
# samples is during TEARDOWN -- Expr_Free/StateCtrl_Free at match unload -- and
# none during gameplay, where this pack allocates ~38 objects per 15 s. So the
# bucket count is an exit-time knob, not a frame-rate one, and measuring it in
# frames was the wrong instrument. This target measures it directly.
#
#   make check-memprof-buckets
#   BUCKETS="1024 262144" make check-memprof-buckets
check-memprof-buckets:
	@sh tools/test_memprof_buckets.sh

.PHONY: check-memprof-buckets

# --- Explods_Count equivalence ----------------------------------------------
# Explods_Count (0x00411790) no longer scans all `capacity` slots: it stops once
# it has seen `list->count` live items, because a character polling `numexplod`
# every tick made the original O(ExplodMax) scan 33.7% of in-match CPU on a heavy
# pack. That is only exact while `count` equals the number of used slots, so this
# gate checks the fast path against the original scan over exhaustive, randomised
# and boundary layouts -- and separately proves the invariant is load-bearing
# (an inconsistent count must make the two diverge, or the test proves nothing).
#
#   make check-explods-count
check-explods-count:
	@sh tools/test_explods_count.sh

.PHONY: check-explods-count

# --- toolchain and dependency checks ----------------------------------------
# The two platforms need different things, and these targets say which is
# which rather than assuming Windows. check-toolchain runs the set that
# belongs to the platform being built; the other checks remain callable.

check-toolchain: check-pkgconfig $(if $(filter win32,$(MUGEN_FAMILY)),check-libs)
	@triple=$$($(CC) -dumpmachine 2>/dev/null); \
	if [ -z "$$triple" ]; then \
	  echo "error: '$(CC) -dumpmachine' produced nothing; is $(CC) installed?"; exit 1; \
	fi; \
	if [ -n '$(TRIPLE_RE)' ] && ! printf '%s\n' "$$triple" | grep -q '$(TRIPLE_RE)'; then \
	  echo "error: $(CC) targets '$$triple', but MUGEN_TARGET=$(MUGEN_TARGET) needs '$(TRIPLE_RE)'"; \
	  echo "       pass CC=<compiler for that target>, or MUGEN_TARGET=<the target $(CC) builds>"; \
	  exit 1; \
	fi; \
	for t in $(AR) $(STRIP) $(if $(filter win32,$(MUGEN_FAMILY)),$(WINDRES)); do \
	  command -v $$t >/dev/null 2>&1 \
	    || { echo "error: $$t not found in PATH"; exit 1; }; \
	done; \
	echo "toolchain ok: $$($(CC) --version | head -1) / $$triple"; \
	echo "target:       $(MUGEN_TARGET) ($(MUGEN_PTRBITS)-bit) -> $(TARGET)"; \
	echo "python: $(if $(strip $(PYTHON3)),$$($(PYTHON3) --version),not found (only needed for MUGEN_GAME_DIR))"
ifeq ($(MUGEN_FAMILY),win32)
	@echo "dependencies: $(if $(strip $(MUGEN_WIN_SYSROOT)),$(MUGEN_WIN_SYSROOT),the kit's own pkg-config setup (no build/sysroots/$(MUGEN_TARGET)-sysroot))"; \
	echo "  packages:     $(STATIC_PKGS)"
endif
ifneq ($(strip $(MUGEN_CROSS)),)
	@echo "cross: $(CROSS_COMPILE)"; \
	echo "  dependency tree: $(MUGEN_CROSS_SYSROOT)"; \
	if [ -n '$(MUGEN_CROSS_COMPILER_SYSROOT)' ]; then \
	  echo "  sysroot flag:    none -- the compiler carries its own ($(MUGEN_CROSS_COMPILER_SYSROOT))"; \
	else \
	  echo "  sysroot flag:    $(MUGEN_CROSS_SYSROOT_FLAGS) (the compiler has no sysroot of its own)"; \
	fi; \
	echo "  packages:        $(STATIC_PKGS) $(MUGEN_CROSS_STATIC)"; \
	echo "  output:          $(TARGET)"
endif

# pkg-config's stderr is suppressed when $(STATIC_PKGS) is expanded, so a
# missing .pc file leaves PKG_LIBS/PKG_CFLAGS empty and the build only fails
# much later, at the link, on undefined Mix_*/TTF_*/SDL_* symbols. Name the
# packages instead. Both platforms use pkg-config (Linux for the distro's
# shared libraries, Windows for the kit's static ones).
check-pkgconfig:
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 \
	  || { echo "error: $(PKG_CONFIG) not found in PATH"; \
	       echo "       install pkg-config, or pass PKG_LIBS=... PKG_CFLAGS=... yourself"; \
	       exit 1; }
	@missing=; for pkg in $(STATIC_PKGS); do \
	  $(PKG_CONFIG) --exists $$pkg 2>/dev/null || missing="$$missing $$pkg"; \
	done; \
	if [ -n "$$missing" ]; then \
	  echo "error: pkg-config cannot find:$$missing"; \
	  echo "       install those development packages (docs/BUILD-MINGW.md,"; \
	  echo "       docs/BUILD-LINUX.md), or pass PKG_LIBS=... PKG_CFLAGS=... yourself"; \
	  exit 1; \
	fi; \
	echo "pkg-config ok: $(STATIC_PKGS)"

# The Windows static link needs real archives, not just .pc files: a pkg-config
# hit with only a DLL behind it links but is not static. Linux links the
# distro's shared libraries, so on that target there is nothing to look for and
# the target says so rather than reporting SDL archives as "missing".
#
# Check the SDL2 archive names in the selected kit's libdir so a half-installed
# kit fails here.
check-libs:
ifneq ($(MUGEN_FAMILY),win32)
	@echo "check-libs: Windows static build only; $(MUGEN_TARGET) links shared libraries"
else
	@test -n "$(SDK_LIB_DIR)" \
	  || { echo "error: pkg-config reports no libdir for '$(firstword $(STATIC_PKGS))'; is the static SDL stack installed?"; exit 1; }
	@for lib in $(CHECK_LIBS); do \
	  test -f "$(SDK_LIB_DIR)/lib$$lib.a" \
	    || { echo "error: $(SDK_LIB_DIR)/lib$$lib.a missing (need static archives, not DLLs)"; exit 1; }; \
	done
	@echo "static libraries ok: $(CHECK_LIBS) in $(SDK_LIB_DIR)"
endif

# --- housekeeping -----------------------------------------------------------

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(ICON_DIR) $(TARGET) $(STRIPPED) $(SFFCLI_BIN)

distclean:
	rm -rf $(BUILD)

# Prints the header comment block: everything after the first line up to the
# first blank line. Not a line range, so the header can grow without help
# silently truncating or spilling into the rules.
help:
	@awk 'NR > 1 { if (/^#/) { sub(/^# ?/, ""); print } else exit }' Makefile

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
