# mugen-unleashed -- GNU Make + GCC (MinGW-w64, 32-bit) STATIC build.
#
# Produces a self-contained engine binary: the engine in src/, the libraries
# in third_party/ built from their original sources, libresample vendored, and
# SDL2 + SDL2_mixer + SDL2_ttf + zlib + pthread linked statically from the
# x86devkit (SDL2_mixer brings mpg123, vorbisfile, FLAC, opus and wavpack with
# it, so MP3/Ogg playback needs no external decoder DLL). No
# Elecbyte.MUGEN.libs and no plugin DLLs are needed to run.
#
# `make SDL_BACKEND=1` still builds the SDL 1.2 backend, which is what the
# log-comparison workflow against the original binary uses.
#
# The static counterpart to the CMake/MSVC build in CMakeLists.txt, which still
# links the game's DLLs. Same sources, different link step.
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
#   make              build $(TARGET): mugen-mingw-<target>-sdl<2|1>.exe on
#                     Windows, mugen-<target> on Linux (SDL2 is the only
#                     backend there)
# (verify with "make help")
#   make -j8          parallel build
#   make install      strip and copy the executable into install/
#   make V=1          show the full command lines
#   make clean        remove the object files of $(BUILD)
#   make distclean    remove $(BUILD) entirely
#   make check-toolchain   verify the compiler and the libraries this target needs
#
# The window icon comes from res/icons/*.ico (see "the window icon" below).
#
# Optional:
#   make MUGEN_GAME_DIR=C:/mugen-1.1b1    embed the original mugen.exe's icon
#                                         instead (needs python3;
#                                         tools/extract_icon.py)
#   make MUGEN_ICONS=res/icons/1.ico      a different icon, or several to merge
#   make BUILD=build/mingw-debug DEBUG=1  unoptimised build with debug info
#   make MUGEN_GLES=0                     force the desktop-GL renderer on a
#                                         Linux GLES target (see below)
#   make CROSS_COMPILE=aarch64-none-linux-gnu- XSYSROOT=build/tmp/arm64-sysroot
#                                         cross-compile the Linux target with a
#                                         foreign toolchain + dependency tree;
#                                         port/arm64/build.sh wraps this
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
#     port/arm64/build.sh              (Windows host, Arm GNU Toolchain)
#
# Equivalently, by hand:
#
#     make CROSS_COMPILE=aarch64-none-linux-gnu- \
#          XSYSROOT=build/tmp/arm64-sysroot MUGEN_TARGET=linux-arm64
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
            use the platform port (port/arm64/build.sh) -- see docs/BUILD-LINUX.md)
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
    # -nostdinc below drops *every* implicit include directory, the toolchain's
    # own C library headers among them (math.h, stdio.h, ...), so the compiler
    # is asked which ones it had and they are put back with -idirafter: after
    # $(XSYSROOT)/usr/include, so a dependency's header always wins over any
    # copy of the same name shipped beside the libc headers.
    MUGEN_CROSS_CC_INCLUDE := $(shell $(CC) $(MUGEN_CROSS_SYSROOT_FLAGS) -E -v -x c /dev/null 2>&1 | \
        sed -n '/^#include <...> search starts here:/,/^End of search list/p' | \
        sed -e '1d' -e '$$d' -e 's/^ \{1,\}//' -e 's/^/-idirafter /' | tr '\n' ' ')
    # The host compiler serves pkg-config; only the .pc files must come from
    # the target tree. Both variables are replaced, not extended.
    #
    # PKG_CONFIG_SYSTEM_* matter as much as PKG_CONFIG_LIBDIR: pkgconf qualifies
    # system include/library paths for the *host* (/mingw64/include), so a
    # target .pc with -I${includedir} came back as the host's path and the build
    # compiled against the host's SDL2 headers. Pointing both variables at the
    # target tree is what makes ${includedir} resolve to the target.
    MUGEN_HOST_PKG_CONFIG ?= $(firstword $(foreach p,pkg-config pkgconf,$(if $(shell command -v $(p) 2>/dev/null && echo $(p)),$(p))))
    ifeq ($(filter default file environment undefined,$(origin PKG_CONFIG)),$(origin PKG_CONFIG))
      PKG_CONFIG := env PKG_CONFIG_LIBDIR='$(MUGEN_CROSS_LIB)/pkgconfig' \
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

# SDL backend: 2 = SDL2 with SDL2_mixer/SDL2_ttf (the default), 1 = SDL 1.2
# with the game's SDL_mixer/SDL_ttf (kept as the reference backend for the
# log-comparison workflow). See SDL2_PLAN.md. The backend changes the include
# path, the packages linked, the object/output directory and the engine's -D
# flags, so a build of one backend can never reuse the other's objects.
#
# SDL 1.2 is still buildable with `make SDL_BACKEND=1`.
SDL_BACKEND ?= 2
ifneq ($(filter 1 2,$(SDL_BACKEND)),$(SDL_BACKEND))
  $(error SDL_BACKEND must be 1 or 2, not "$(SDL_BACKEND)")
endif
# The output name marks the non-default backend, not the default one: SDL2 is
# The output name always marks the backend: SDL2 is the default and gets
# -sdl2, an SDL 1.2 build gets -sdl1. Object and output directories follow the
# same split, so the two backends can be built side by side without reusing
# each other's objects.
#
# Only the win32 target supports the SDL 1.2 backend (the reference backend
# for the log-comparison workflow); SDL2-only targets reject SDL_BACKEND=1 so
# their names stay within the approved set.
SDL_BACKEND_TAG := $(if $(filter 1,$(SDL_BACKEND)),sdl1,sdl2)

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
  BUILD    ?= build/mingw-$(SDL_BACKEND_TAG)
  TARGET   := $(BUILD)/mugen-mingw-win32-$(SDL_BACKEND_TAG).exe
else ifeq ($(MUGEN_TARGET),win64)
  MUGEN_FAMILY := win32
  MUGEN_PTRBITS := 64
  BUILD    ?= build/mingw64-$(SDL_BACKEND_TAG)
  TARGET   := $(BUILD)/mugen-mingw-win64-$(SDL_BACKEND_TAG).exe
else
  MUGEN_FAMILY := linux
  MUGEN_PTRBITS := $(if $(findstring 64,$(MUGEN_TARGET)),64,32)
  # A cross build names its own directory: same triple, different toolchain and
  # dependency tree, so its objects must not be reused from the host build.
  ifneq ($(strip $(CROSS_COMPILE)),)
    MUGEN_CROSS_TAG := -$(patsubst %-,%,$(CROSS_COMPILE))
  endif
  BUILD    ?= build/$(MUGEN_TARGET)$(MUGEN_CROSS_TAG)-$(SDL_BACKEND_TAG)
  TARGET   := $(BUILD)/mugen-$(MUGEN_TARGET)$(MUGEN_CROSS_TAG)
endif

# SDL 1.2 is only wired up for the win32 target; everywhere else it is SDL2
# or nothing, so the name stays within the approved set.
ifeq ($(SDL_BACKEND),1)
ifneq ($(MUGEN_TARGET),win32)
  ifeq ($(MUGEN_TARGET),win64)
    $(error SDL_BACKEND=1 is only supported for MUGEN_TARGET=win32; $(CC) targets $(TRIPLE), so the \
            64-bit target was selected -- the SDL 1.2 reference backend needs the i686 kit \
            (x86devkit on PATH, or CC=i686-w64-mingw32-gcc))
  else
    $(error SDL_BACKEND=1 is only supported for MUGEN_TARGET=win32, not "$(MUGEN_TARGET)")
  endif
endif
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
CONFIG_ID    := backend=$(SDL_BACKEND) target=$(MUGEN_TARGET) gles=$(MUGEN_GLES)
ifneq ($(strip $(MUGEN_CROSS)),)
# Port fix: the prefix is identical for Arm 9.2 and 15.3. Record the resolved
# compiler and version so selecting another kit cannot reuse the old binary.
CONFIG_ID += cc=$(shell command -v $(CC)) version=$(shell $(CC) -dumpfullversion -dumpversion) sysroot=$(MUGEN_CROSS_SYSROOT)
endif
ICON_DIR  := $(BUILD)/icon
# TARGET is set per platform in the detection block above.

# The static SDL stack comes from the x86devkit through pkg-config, which also
# pulls each library's own static dependencies (freetype, bz2, ogg, vorbis,
# FLAC, opus, wavpack, gme, mpg123, zlib, pthread, stdc++ ...). Asking
# pkg-config for --libs --static is what makes this link close: hand-listing
# -lSDL_mixer -lSDL_ttf would miss a dozen transitive archives.
# SDL_image is deliberately absent: the engine loads PNG itself (src/pngio.c
# via <png.h> in mugen.h) and never calls IMG_*, so nothing needs it. It was
# only ever there because SDL_image.pc happens to list libpng16, which used to
# collide with the vendored libpng 1.2.38. zlib and libpng16 are now asked for
# directly instead of arriving as someone else's transitive dependency.
STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng16
# -mwindows is stripped from the pkg-config output and added once at the end of
# the link line rather than repeated per package.
#
# Note: the resulting exe still exports the SDL/Mixer/TTF function names. That
# comes from .drectve sections inside libSDL.a asking for them, and a command
# line --exclude-all-symbols does not override a .drectve. Only library names
# are exported, never engine symbols, so it is harmless; removing them would
# mean rebuilding SDL without its export directives.
PKG_LIBS    := $(filter-out -mwindows,$(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null))
PKG_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null)
SDK_LIB_DIR ?= $(shell $(PKG_CONFIG) --variable=libdir $(firstword $(STATIC_PKGS)) 2>/dev/null)

# The static archives check-libs looks for, per backend. SDL 1.2 keeps the
# add-ons in separate libraries (SDL_gfx has no pkg-config libs of its own on
# every kit); SDL2 has SDL2_mixer/SDL2_ttf. mpg123 comes in transitively and is
# listed here because a kit missing it links but plays no MP3.
ifeq ($(SDL_BACKEND),2)
  CHECK_LIBS := SDL2 SDL2_mixer SDL2_ttf z mpg123
else
  CHECK_LIBS := SDL SDL_gfx SDL_mixer SDL_ttf z mpg123
endif

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

ENGINE_WARN := -Wall -Wno-unused-parameter -Wno-unused-result \
               -Wno-stringop-truncation

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
    # Windows paths: the MSVC SDK tree is part of the build, and the compat layer
    # is not used at all.
    PLAT_CPPFLAGS := -I$(SDK_DIR)/include
  endif
  LINK_MODE   := -mwindows
  STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng16
  PKG_LIBS    := $(filter-out -mwindows,$(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null))
  PKG_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null)
  SDL_CFLAGS  :=
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
  # No --static on Linux: the distro's shared SDL is the normal thing to link
  # against, and SDL_mixer.so already carries its own codec dependencies.
  STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng
  # Cross-compiling (CROSS_COMPILE=... XSYSROOT=...): the sd2 branch below
  # replaces STATIC_PKGS/PKG_LIBS anyway, and MUGEN_CROSS_STATIC decides
  # --static. Here the only difference is that -lGL/-lGLU (which a GLES cross
  # target has none of) are added below only when MUGEN_CROSS is unset.
  ifneq ($(strip $(MUGEN_CROSS_SYSROOT)),)
    PKG_LIBS  := $(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null)
  else
    PKG_LIBS  := $(shell $(PKG_CONFIG) --libs $(STATIC_PKGS) 2>/dev/null) -lGL -lGLU -ldl -lm
  endif
  PKG_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null)
  # SDL 1.2 headers live in /usr/include/SDL for the distro package.
  SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl 2>/dev/null)
  SYS_LIBS    := -lpthread
  # The MSVC SDK headers are Windows-only and must not be on this path.
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

# --- SDL backend selection --------------------------------------------------
#
# SDL_BACKEND=2 (the default) uses SDL2 and its add-ons and turns on
# MUGEN_SDL2, which makes mugen.h pull in src/sdl2compat.h: the removed SDL
# 1.2 entry points (SDL_SetVideoMode, SDL_Flip, SDL_WM_*, ...) are reimplemented
# over SDL2 there, so the reconstructed call sites in src/ keep their shape.
# SDL_BACKEND=1 selects the older SDL 1.2 stack instead; it is kept for the
# reference build and the log comparison, not as the normal target.
#
# Two pkg-config outputs must be edited, not used verbatim:
#   -Dmain=SDL_main  sdl2.pc renames main; this build keeps the reconstructed
#                    WinMain in src/sdlmain.c, which calls main() itself, so the
#                    rename must not happen (SDL_MAIN_HANDLED semantics).
#   -lSDL2main       the same reasoning: SDL2main's WinMain would clash with the
#                    reconstructed one.
# Everything else (the -mwindows flag, SDL2's own transitive imports) is kept.
ifeq ($(SDL_BACKEND),2)
  ifeq ($(MUGEN_FAMILY),win32)
    STATIC_PKGS := sdl2 SDL2_mixer SDL2_ttf zlib libpng16
    PKG_LIBS    := $(filter-out -mwindows -lSDL2main,$(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null))
  else
    STATIC_PKGS := sdl2 SDL2_mixer SDL2_ttf zlib libpng
    # GLES targets link no desktop GL: the Mali/EGL stack has no GLX, and
    # -lGL here is what made the failure look like a missing driver instead of
    # a wrong backend (PROBLEMS.md P1). EGL comes in through SDL2 itself.
    #
    # A cross build asks pkg-config for the static close (--static), because
    # the dependency tree holds archives: there is no target sysroot at run
    # time to resolve shared objects from, and SDL2's own link line
    # (-pthread -lm) has to be carried along. A GLES cross target still has no
    # -lGL: the tree ships no desktop GL at all.
    ifeq ($(MUGEN_GLES),1)
      PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(MUGEN_CROSS_STATIC) $(STATIC_PKGS) 2>/dev/null) -ldl -lm
    else
      PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(MUGEN_CROSS_STATIC) $(STATIC_PKGS) 2>/dev/null) -lGL -lGLU -ldl -lm
    endif
  endif
  PKG_CFLAGS := $(filter-out -Dmain=SDL_main,$(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null))
  SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
  # SDL_MAIN_HANDLED: told before <SDL.h>, so SDL2 leaves main() alone. The
  # engine keeps its own entry points (src/sdlmain.c on Windows, src/main.c on
  # Linux) exactly as the SDL 1.2 build does.
  ENGINE_DEFS += -DMUGEN_SDL2 -DSDL_MAIN_HANDLED
endif

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
  GLES_SHADER_SRCS := $(wildcard res/shaders/gles/*.vert res/shaders/gles/*.frag)
else
  GLAD_DIR := $(TP_DIR)/glad-2.1
  GLAD_SRC := $(GLAD_DIR)/glad.c
  ENGINE_EXCLUDE_SRCS :=
  GLES_SHADER_SRCS :=
endif

# SANITIZE=address (or undefined,thread) instruments the engine and links the
# runtime. Used while bringing up a new platform; not for shipping builds.
ifneq ($(SANITIZE),)
  OPT += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
  SYS_LIBS += -fsanitize=$(SANITIZE)
endif

# A cross build puts the target's include tree first and -- with -nostdinc --
# leaves only that plus the toolchain's own headers reachable. -isystem keeps
# the dependency headers out of the -Wall/-W3 warnings: they are not ours.
CPPFLAGS += $(CROSS_CPPFLAGS) \
            -I$(SRC_DIR) \
            -I$(TP_DIR)/libresample/include \
            $(PLAT_CPPFLAGS) \
            -I$(TP_DIR)/lua-5.1.4/src \
            -I$(GLAD_DIR) \
            -I$(TP_DIR)/minizip-0.15 \
            $(PKG_CFLAGS) $(SDL_CFLAGS)

# FORCE_INCLUDE stays out of COMMONFLAGS: it supplies Windows type names
# (BOOL, WORD, ...) that clash with the same names in the X11 headers a
# third-party GL library can pull in. Only the engine needs it.
COMMONFLAGS := $(ARCHFLAGS) -std=gnu11 $(FPUFLAGS) \
               -fno-strict-aliasing -MMD -MP $(OPT)

ENGINE_CFLAGS := $(COMMONFLAGS) $(FORCE_INCLUDE) $(ENGINE_WARN) $(ENGINE_DEFS) $(BUILD_DEFS)
# Old third-party code: its warnings are not ours to fix, as /W0 in CMake.
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
# src/sdlmixercompat.c is not an original module: it supplies Mix_QuerySpecEx,
# which only Elecbyte's SDL_mixer build exported. Linked statically here, not in
# the CMake build, which uses the game's real SDL_mixer.dll.
#
# ENGINE_EXCLUDE_SRCS removes the desktop renderer under MUGEN_GLES (see the
# GLES backend block above); it is empty for every other build.
ENGINE_SRCS := $(filter-out $(ENGINE_EXCLUDE_SRCS),$(wildcard $(SRC_DIR)/*.c))
ENGINE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/src/%.o,$(ENGINE_SRCS))

# Third-party libraries, built from source exactly as CMakeLists.txt and
# cmake/ThirdParty.cmake build them.
LUA_SRCS := $(wildcard $(TP_DIR)/lua-5.1.4/src/*.c)

# libresample 0.1.3, vendored and ported to multi-channel to match the ABI of
# the shipped libresample.dll (see third_party/libresample/VENDORED.md).
LRS_SRCS := $(wildcard $(TP_DIR)/libresample/src/*.c)

LUA_OBJS := $(patsubst $(TP_DIR)/lua-5.1.4/src/%.c,$(OBJ_DIR)/lua/%.o,$(LUA_SRCS))
LRS_OBJS := $(patsubst $(TP_DIR)/libresample/src/%.c,$(OBJ_DIR)/libresample/%.o,$(LRS_SRCS))
GLAD_OBJ := $(OBJ_DIR)/glad/glad.o
MINIZIP_OBJ := $(OBJ_DIR)/minizip/unzip.o

LUA_LIB := $(LIB_DIR)/liblua.a
LRS_LIB := $(LIB_DIR)/libresample.a
TP_LIBS := $(LUA_LIB) $(LRS_LIB) $(GLAD_OBJ) $(MINIZIP_OBJ)

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

.PHONY: all clean distclean check-toolchain check-libs check-pkgconfig help install perf
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
$(TARGET): $(ENGINE_OBJS) $(RC_OBJ) $(TP_LIBS)
	@echo "  LD      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ARCHFLAGS) -o $@ $(ENGINE_OBJS) $(RC_OBJ) \
	    $(PLAT_LDFLAGS) $(CROSS_LDFLAGS) \
	    -L$(LIB_DIR) -llua -lresample \
	    $(GLAD_OBJ) $(MINIZIP_OBJ) \
	    $(PKG_LIBS) $(SYS_LIBS) \
	    $(LINK_MODE)

# --- the engine -------------------------------------------------------------

# Writes $(CONFIG_STAMP) whenever $(CONFIG_ID) changes. FORCE forces the recipe
# to run on every build; the file is rewritten only when the content differs, so
# an unchanged configuration leaves every object's timestamp alone and nothing is
# recompiled. This is what makes switching SDL_BACKEND (or the target/arch) in an
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

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c $(CONFIG_STAMP)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -c $< -o $@

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

ifeq ($(ICON_ICO),)
$(ALL_RC): $(SRC_DIR)/mugen.rc $(MANIFEST)
else
$(ALL_RC): $(SRC_DIR)/mugen.rc $(MANIFEST) $(ICON_ICO)
endif
	@echo "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)cat $(SRC_DIR)/mugen.rc > $@
	$(Q)printf '\n/* generated: the assembly manifest, RT_MANIFEST 1 */\n1 24 "%s"\n' \
	    "$(CURDIR)/$(MANIFEST)" >> $@

# windres runs its own preprocessor, so it needs the define on its command line
# too: the .rc includes icon 101 only under MUGEN_HAS_ICON, and passing it to the
# C compiler alone left the resource out entirely (no RT_GROUP_ICON, no RT_ICON,
# and an executable whose icon silently stayed the default). CMake does this
# through target_compile_definitions; this is the equivalent.
RC_DEFS :=
ifneq ($(ICON_ICO),)
RC_DEFS += -DMUGEN_HAS_ICON
endif

$(RC_OBJ): $(ALL_RC) $(CONFIG_STAMP)
	@echo "  WINDRES $<"
	$(Q)$(WINDRES) $(RC_DEFS) -I$(SRC_DIR) -I$(ICON_DIR) -i $< -o $@

# --- install ----------------------------------------------------------------
# Strips the executable and copies it into the game directory, where it stays.
# install/ is the shipped M.U.G.E.N 1.1b1 tree (git-ignored). The installed
# name follows $(TARGET), so it is never the original's mugen.exe and never a
# Windows name on Linux: mugen-mingw-*.exe on Windows, mugen-linux-<arch> on
# Linux (an ELF binary called .exe was the old behaviour here). The name marks
# the backend, so installing one backend never clobbers the other.

INSTALL_DIR  ?= install
INSTALL_NAME ?= $(notdir $(TARGET))
STRIPPED     := $(BUILD)/$(basename $(INSTALL_NAME))-stripped$(suffix $(INSTALL_NAME))

$(STRIPPED): $(TARGET)
	@echo "  STRIP   $@"
	$(Q)$(STRIP) -o $@ $(TARGET)

install: $(STRIPPED)
	@echo "  INSTALL $(INSTALL_DIR)/$(INSTALL_NAME)"
	$(Q)mkdir -p $(INSTALL_DIR)
	$(Q)cp $(STRIPPED) $(INSTALL_DIR)/$(INSTALL_NAME)
ifeq ($(MUGEN_GLES),1)
# The GLES renderer loads GLSL ES sources from data/gles/*, next to the game's
# own data/ (src/gles_render.c gles_read_shader). They are ours, not game
# content, so they ship with the binary rather than coming from the
# installation -- the same reason port/android/build.sh stages them into the APK.
	@echo "  SHADERS $(INSTALL_DIR)/data/gles"
	$(Q)mkdir -p $(INSTALL_DIR)/data/gles
	$(Q)cp -f $(GLES_SHADER_SRCS) $(INSTALL_DIR)/data/gles/
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
# The archive names follow the SDL backend and the pointer width: SDL 1.2 is
# libSDL.a, SDL2 is libSDL2.a, and a 64-bit kit may keep them under a different
# libdir. The names are checked exactly so a half-installed kit fails here.
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
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(ICON_DIR) $(TARGET) $(STRIPPED)

distclean:
	rm -rf $(BUILD)

# Prints the header comment block: everything after the first line up to the
# first blank line. Not a line range, so the header can grow without help
# silently truncating or spilling into the rules.
help:
	@awk 'NR > 1 { if (/^#/) { sub(/^# ?/, ""); print } else exit }' Makefile

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
