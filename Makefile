# mugen-unleashed -- GNU Make + GCC (MinGW-w64, 32-bit) STATIC build.
#
# Produces a self-contained mugen-static.exe: the engine in src/, the libraries
# in third_party/ built from their original sources, libresample vendored, and
# SDL 1.2 + SDL_gfx + SDL_mixer + SDL_ttf + zlib + pthread + mpg123 linked
# statically from the x86devkit. No Elecbyte.MUGEN.libs needed to run.
#
# The static counterpart to the CMake/MSVC build in CMakeLists.txt, which still
# links the game's DLLs. Same sources, different link step.
#
# The Windows target is a 32-bit GUI executable. It must be built with an
# i686-w64-mingw32 toolchain (x86_64-w64-mingw32 will not do: the engine is a
# 32-bit binary reconstruction, its struct layouts and pointers are 4 bytes).
#
# The same Makefile also builds a Linux target, chosen from uname and the
# compiler's triple (docs/BUILD-LINUX.md). There the distro's shared SDL is
# linked instead of the kit's static archives, so the executable is
# mugen-linux-<arch> and both checks below follow the platform being built.
#
#   make              build $(TARGET): mugen-static.exe or mugen-linux-<arch>
#   make -j8          parallel build
#   make install      strip and copy the executable into install/
#   make V=1          show the full command lines
#   make clean        remove the object files of $(BUILD)
#   make distclean    remove $(BUILD) entirely
#   make check-toolchain   verify the compiler and the libraries this target needs
#
# Optional:
#   make MUGEN_GAME_DIR=C:/mugen-1.1b1    embed the original's window icon
#                                         (needs python3; see tools/extract_icon.py)
#   make BUILD=build/mingw-debug DEBUG=1  unoptimised build with debug info
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
# Python is only needed for the optional icon (tools/extract_icon.py).
# Existing is not enough: on Windows 'python3' is usually the Microsoft Store
# alias, which "exists" and prints an advert instead of running, so every
# candidate is asked to run. Without MUGEN_GAME_DIR Python is never used, so
# its absence is not an error.
PYTHON3 ?= $(firstword $(foreach p,python3 python py,$(if $(shell $(p) -c "pass" 2>/dev/null && echo ok),$(p))))

DEBUG    ?=

MUGEN_GAME_DIR ?=

# SDL backend: 1 = SDL 1.2 (the default and the reference), 2 = SDL2 with
# SDL2_mixer/SDL2_ttf. See SDL2_PLAN.md. The backend changes the include path,
# the packages linked, the object/output directory and the engine's -D flags,
# so a build of one backend can never reuse the other's objects.
SDL_BACKEND ?= 1
ifneq ($(filter 1 2,$(SDL_BACKEND)),$(SDL_BACKEND))
  $(error SDL_BACKEND must be 1 or 2, not "$(SDL_BACKEND)")
endif
SDL2_SUFFIX := $(if $(filter 2,$(SDL_BACKEND)),-sdl2)

# ------------------------------------------------------- platform detection
#
# uname -s decides the OS family; the compiler's own target triple decides the
# CPU and ABI. uname -m must NOT decide the architecture: inside MSYS it reports
# the host (x86_64 here) while the compiler targets i686-w64-mingw32, so
# trusting it would select 64-bit flags for a binary this project cannot link.
# The compiler wins on any disagreement.
#
# Override for cross-compiling:  make MUGEN_TARGET=linux-arm64

UNAME_S := $(shell uname -s 2>/dev/null)
TRIPLE  := $(shell $(CC) -dumpmachine 2>/dev/null)

ifeq ($(origin MUGEN_TARGET),undefined)
  ifneq (,$(filter Windows_NT MINGW% MSYS% CYGWIN% NT,$(UNAME_S)))
    ifneq (,$(findstring i686-w64-mingw32,$(TRIPLE)))
      MUGEN_TARGET := win32
    else ifneq (,$(findstring x86_64-w64-mingw32,$(TRIPLE)))
      $(error $(TRIPLE) cannot link this 32-bit target (no 32-bit CRT in that kit); use the i686 kit)
    else
      $(error unsupported Windows toolchain "$(TRIPLE)"; expected i686-w64-mingw32)
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

# One family per target: win32 / linux.
ifeq ($(MUGEN_TARGET),win32)
  MUGEN_FAMILY := win32
  BUILD    ?= build/mingw$(SDL2_SUFFIX)
  TARGET   := $(BUILD)/mugen-static$(SDL2_SUFFIX).exe
else
  MUGEN_FAMILY := linux
  BUILD    ?= build/$(MUGEN_TARGET)$(SDL2_SUFFIX)
  TARGET   := $(BUILD)/mugen-$(MUGEN_TARGET)$(SDL2_SUFFIX)
endif

# What check-toolchain expects `$(CC) -dumpmachine` to start with. Detection
# above already rejects a mismatched pair, but an explicit MUGEN_TARGET skips
# detection, and a wrong CC then silently builds for the wrong machine -- the
# one mistake worth catching. Empty means "no expectation" (unknown target).
ifeq ($(MUGEN_TARGET),win32)
  TRIPLE_RE := ^i686-w64-mingw32
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
# Where pkg-config's prefix puts the archives; only used by check-libs.
SDK_LIB_DIR ?= $(shell $(PKG_CONFIG) --variable=libdir sdl 2>/dev/null)

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
# -Wno-error=incompatible-pointer-types: src/globals.c stores an `int (void)`
# function in a `void (void)` slot of the BGM subsystem table (the original only
# ever calls it for its side effects). MSVC accepts it as a warning, GCC 14+
# rejects it as an error by default.
#
# -Wno-stringop-truncation: the reconstruction copies into fixed-size fields
# with strncpy/strncat exactly as the original did (the original relies on the
# zero-filled struct, not on NUL termination).

ENGINE_WARN := -Wall -Wno-unused-parameter -Wno-unused-result \
               -Wno-stringop-truncation \
               -Wno-error=incompatible-pointer-types

ifeq ($(DEBUG),1)
  OPT     := -O0 -g3
  BUILD_DEFS := -DMUGEN_DEBUG
else
  OPT     := -O2 -g
  BUILD_DEFS :=
endif

LRS_DEFS := -DLIBRESAMPLE_STATIC

ifeq ($(MUGEN_FAMILY),win32)
  ARCHFLAGS := -m32
  FPUFLAGS  := -mfpmath=387 -fexcess-precision=standard
  # PTW32_STATIC_LIB: third_party/sdk/include/pthread.h declares the pthread
  # functions __declspec(dllimport) unless this is set; we link pthreadVC2 as a
  # static archive. LIBRESAMPLE_STATIC does the same for libresample.h.
  ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE \
                 -DPTW32_STATIC_LIB $(LRS_DEFS)
  LINK_MODE   := -mwindows
  STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng16
  PKG_LIBS    := $(filter-out -mwindows,$(shell $(PKG_CONFIG) --libs --static $(STATIC_PKGS) 2>/dev/null))
  PKG_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null)
  SDL_CFLAGS  :=
  SYS_LIBS    := -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32
  # Windows paths: the MSVC SDK tree is part of the build, and the compat layer
  # is not used at all.
  PLAT_CPPFLAGS := -I$(SDK_DIR)/include
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
  PKG_LIBS    := $(shell $(PKG_CONFIG) --libs $(STATIC_PKGS) 2>/dev/null) -lGL -lGLU -ldl -lm
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
endif

# --- SDL backend selection --------------------------------------------------
#
# SDL_BACKEND=2 swaps the SDL 1.2 packages for SDL2 and its add-ons and turns
# on MUGEN_SDL2, which makes mugen.h pull in src/sdl2compat.h: the removed SDL
# 1.2 entry points (SDL_SetVideoMode, SDL_Flip, SDL_WM_*, ...) are reimplemented
# over SDL2 there, so the reconstructed call sites in src/ keep their shape.
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
    PKG_LIBS    := $(shell $(PKG_CONFIG) --libs $(STATIC_PKGS) 2>/dev/null) -lGL -lGLU -ldl -lm
  endif
  PKG_CFLAGS := $(filter-out -Dmain=SDL_main,$(shell $(PKG_CONFIG) --cflags $(STATIC_PKGS) 2>/dev/null))
  SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
  # SDL_MAIN_HANDLED: told before <SDL.h>, so SDL2 leaves main() alone. The
  # engine keeps its own entry points (src/sdlmain.c on Windows, src/main.c on
  # Linux) exactly as the SDL 1.2 build does.
  ENGINE_DEFS += -DMUGEN_SDL2 -DSDL_MAIN_HANDLED
endif

# SANITIZE=address (or undefined,thread) instruments the engine and links the
# runtime. Used while bringing up a new platform; not for shipping builds.
ifneq ($(SANITIZE),)
  OPT += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
  SYS_LIBS += -fsanitize=$(SANITIZE)
endif

CPPFLAGS += -I$(SRC_DIR) \
            -I$(TP_DIR)/libresample/include \
            $(PLAT_CPPFLAGS) \
            -I$(TP_DIR)/lua-5.1.4/src \
            -I$(TP_DIR)/glad-2.1 \
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

# --- the window icon of the original, only meaningful on Windows --------------
ifeq ($(MUGEN_FAMILY),win32)
ifneq ($(MUGEN_GAME_DIR),)
  ifneq ($(wildcard $(MUGEN_GAME_DIR)/mugen.exe),)
    ifeq ($(strip $(PYTHON3)),)
      $(error MUGEN_GAME_DIR is set but no working Python was found; set PYTHON3=...)
    endif
    ICON_ICO := $(ICON_DIR)/mugen.ico
    ENGINE_CFLAGS += -DMUGEN_HAS_ICON -I$(ICON_DIR)
  endif
endif
endif

# ------------------------------------------------------------------- sources

# The engine, one file per module of the original (see docs/ANALYSIS.md).
# src/sdlmixercompat.c is not an original module: it supplies Mix_QuerySpecEx,
# which only Elecbyte's SDL_mixer build exported. Linked statically here, not in
# the CMake build, which uses the game's real SDL_mixer.dll.
ENGINE_SRCS := $(wildcard $(SRC_DIR)/*.c)
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

.PHONY: all clean distclean check-toolchain check-libs check-pkgconfig help install
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
	    $(PLAT_LDFLAGS) \
	    -L$(LIB_DIR) -llua -lresample \
	    $(GLAD_OBJ) $(MINIZIP_OBJ) \
	    $(PKG_LIBS) $(SYS_LIBS) \
	    $(LINK_MODE)

# --- the engine -------------------------------------------------------------

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ENGINE_CFLAGS) $(CPPFLAGS) -c $< -o $@

# --- Lua 5.1.4 --------------------------------------------------------------

$(OBJ_DIR)/lua/%.o: $(TP_DIR)/lua-5.1.4/src/%.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/lua-5.1.4/src -c $< -o $@

$(LUA_LIB): $(LUA_OBJS)
	@echo "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

# --- libresample 0.1.3 (vendored, multi-channel) ----------------------------
# -DLIBRESAMPLE_STATIC turns off __declspec(dllimport) in the header.

$(OBJ_DIR)/libresample/%.o: $(TP_DIR)/libresample/src/%.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -DLIBRESAMPLE_STATIC \
	    -I$(TP_DIR)/libresample/include -I$(TP_DIR)/libresample/src -c $< -o $@

$(LRS_LIB): $(LRS_OBJS)
	@echo "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

# --- glad 2.1 ---------------------------------------------------------------
# Generated for this engine: GL 2.1 compatibility profile plus
# ARB_fragment_program, ARB_texture_non_power_of_two, EXT_blend_logic_op and
# EXT_framebuffer_object. One self-contained .c, no import library.

$(GLAD_OBJ): $(TP_DIR)/glad-2.1/glad.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/glad-2.1 -c $< -o $@

# --- minizip (unzip 0.15) ---------------------------------------------------

$(MINIZIP_OBJ): $(TP_DIR)/minizip-0.15/unzip.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/minizip-0.15 -I$(SDK_DIR)/include -c $< -o $@

# --- resources --------------------------------------------------------------

$(ICON_ICO):
	@echo "  ICON    $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(PYTHON3) tools/extract_icon.py "$(MUGEN_GAME_DIR)/mugen.exe" $@

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

$(RC_OBJ): $(ALL_RC)
	@echo "  WINDRES $<"
	$(Q)$(WINDRES) -I$(SRC_DIR) -I$(ICON_DIR) -i $< -o $@

# --- install ----------------------------------------------------------------
# Strips the executable and copies it into the game directory, where it stays.
# install/ is the shipped M.U.G.E.N 1.1b1 tree (git-ignored). The installed
# name follows $(TARGET), so it is never the original's mugen.exe and never a
# Windows name on Linux: mugen-static.exe on Windows, mugen-linux-<arch> on
# Linux (an ELF binary called .exe was the old behaviour here).

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
	@echo "  done: run it from $(INSTALL_DIR)"
ifeq ($(MUGEN_FAMILY),win32)
	@echo "        (no Elecbyte.MUGEN.libs needed)"
endif

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
	echo "python: $(if $(strip $(PYTHON3)),$$($(PYTHON3) --version),not found (only needed for MUGEN_GAME_DIR))"

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
check-libs:
ifneq ($(MUGEN_FAMILY),win32)
	@echo "check-libs: Windows static build only; $(MUGEN_TARGET) links shared libraries"
else
	@test -n "$(SDK_LIB_DIR)" \
	  || { echo "error: pkg-config reports no libdir for 'sdl'; is the static SDL stack installed?"; exit 1; }
	@for lib in SDL SDL_gfx SDL_mixer SDL_ttf z mpg123; do \
	  test -f "$(SDK_LIB_DIR)/lib$$lib.a" \
	    || { echo "error: $(SDK_LIB_DIR)/lib$$lib.a missing (need static archives, not DLLs)"; exit 1; }; \
	done
	@echo "static libraries ok: SDL SDL_gfx SDL_mixer SDL_ttf z mpg123 in $(SDK_LIB_DIR)"
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
