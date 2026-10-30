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
# The target is a 32-bit Windows GUI executable. It must be built with an
# i686-w64-mingw32 toolchain (x86_64-w64-mingw32 will not do: the engine is a
# 32-bit binary reconstruction, its struct layouts and pointers are 4 bytes).
#
#   make              build $(BUILD)/mugen-static.exe
#   make -j8          parallel build
#   make install      strip and copy the executable into install/
#   make V=1          show the full command lines
#   make clean        remove the object files of $(BUILD)
#   make distclean    remove $(BUILD) entirely
#   make check-toolchain   verify the toolchain is 32-bit MinGW and the static libs exist
#
# Optional:
#   make MUGEN_GAME_DIR=C:/mugen-1.1b1    embed the original's window icon
#                                         (needs python3; see tools/extract_icon.py)
#   make BUILD=build/mingw-debug DEBUG=1  unoptimised build with debug info
#
# See docs/BUILD-MINGW.md for what the dependencies are and where they come from.

# ---------------------------------------------------------------- configuration

CC       ?= gcc
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

BUILD    ?= build/mingw
DEBUG    ?=

MUGEN_GAME_DIR ?=

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
TARGET    := $(BUILD)/mugen-static.exe

# The static SDL stack comes from the x86devkit through pkg-config, which also
# pulls each library's own static dependencies (freetype, bz2, ogg, vorbis,
# FLAC, opus, wavpack, gme, mpg123, zlib, pthread, stdc++ ...). Asking
# pkg-config for --libs --static is what makes this link close: hand-listing
# -lSDL_mixer -lSDL_ttf would miss a dozen transitive archives.
STATIC_PKGS := sdl SDL_gfx SDL_mixer SDL_ttf
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
# x87 FPU at 53-bit precision with /arch:IA32. -mfpmath=387 (the default for
# i686) keeps float arithmetic on the x87, and -fexcess-precision=standard
# rounds on every assignment to a float, which is what the code in src/
# assumes (see src/ftol.h and docs/ANALYSIS.md, "Peculiarities of the binary").
#
# -fno-strict-aliasing: the reconstruction aliases through unions/pointers the
# way the original machine code did; do not let GCC exploit TBAA.
#
# -Wno-error=incompatible-pointer-types: src/globals.c stores an `int (void)`
# function in a `void (void)` slot of the BGM subsystem table (the original
# only ever calls it for its side effects). MSVC accepts it as a warning,
# GCC 14+ rejects it as an error by default.
#
# -Wno-stringop-truncation: the reconstruction copies into fixed-size fields
# with strncpy/strncat exactly as the original did (the original relies on the
# zero-filled struct, not on NUL termination).
# PTW32_STATIC_LIB: third_party/sdk/include/pthread.h declares the pthread
# functions __declspec(dllimport) unless this is set. We link pthreadVC2 as a
# static archive (see the header's own note at PTW32_DLLPORT), so it must be set.
# LIBRESAMPLE_STATIC does the same job for libresample.h.
ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE \
               -DPTW32_STATIC_LIB -DLIBRESAMPLE_STATIC

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

# SDL 1.2 headers come from the kit (pkg-config), not third_party/sdk/include/
# SDL: they are the same 1.2.15 API and the kit's copy matches the libraries we
# now link. third_party/sdk/include/SDL is left in place for the MSVC build.
# $(TP_DIR)/libresample/include MUST come before $(SDK_DIR)/include:
# third_party/sdk/include/libresample.h declares the functions
# __declspec(dllimport) for the game's DLL, which is wrong for a static link.
CPPFLAGS += -I$(SRC_DIR) \
            -I$(TP_DIR)/libresample/include \
            -I$(SDK_DIR)/include \
            -I$(TP_DIR)/lua-5.1.4/src \
            -I$(TP_DIR)/libpng-1.2.38 \
            -I$(TP_DIR)/glew-1.5.4/include \
            -I$(TP_DIR)/minizip-0.15 \
            $(PKG_CFLAGS)

COMMONFLAGS := -m32 -std=gnu11 -mfpmath=387 -fexcess-precision=standard \
               -fno-strict-aliasing -MMD -MP $(OPT)

ENGINE_CFLAGS := $(COMMONFLAGS) $(ENGINE_WARN) $(ENGINE_DEFS) $(BUILD_DEFS)
# Old third-party code: its warnings are not ours to fix, as /W0 in CMake.
TP_CFLAGS     := $(COMMONFLAGS) -w $(ENGINE_DEFS)

# The window icon of the original, when it is available.
ifneq ($(MUGEN_GAME_DIR),)
  ifneq ($(wildcard $(MUGEN_GAME_DIR)/mugen.exe),)
    ifeq ($(strip $(PYTHON3)),)
      $(error MUGEN_GAME_DIR is set but no working Python was found; set PYTHON3=...)
    endif
    ICON_ICO := $(ICON_DIR)/mugen.ico
    ENGINE_CFLAGS += -DMUGEN_HAS_ICON -I$(ICON_DIR)
  endif
endif

# Windows system libraries that SDL's own .pc files do not list.
SYS_LIBS   := -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32

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
PNG_SRCS := $(filter-out %/pngtest.c,$(wildcard $(TP_DIR)/libpng-1.2.38/png*.c))

# libresample 0.1.3, vendored and ported to multi-channel to match the ABI of
# the shipped libresample.dll (see third_party/libresample/VENDORED.md).
LRS_SRCS := $(wildcard $(TP_DIR)/libresample/src/*.c)

LUA_OBJS := $(patsubst $(TP_DIR)/lua-5.1.4/src/%.c,$(OBJ_DIR)/lua/%.o,$(LUA_SRCS))
PNG_OBJS := $(patsubst $(TP_DIR)/libpng-1.2.38/%.c,$(OBJ_DIR)/libpng/%.o,$(PNG_SRCS))
LRS_OBJS := $(patsubst $(TP_DIR)/libresample/src/%.c,$(OBJ_DIR)/libresample/%.o,$(LRS_SRCS))
GLEW_OBJ := $(OBJ_DIR)/glew/glew.o
MINIZIP_OBJ := $(OBJ_DIR)/minizip/unzip.o

LUA_LIB := $(LIB_DIR)/liblua.a
PNG_LIB := $(LIB_DIR)/libpng.a
LRS_LIB := $(LIB_DIR)/libresample.a
TP_LIBS := $(LUA_LIB) $(PNG_LIB) $(LRS_LIB) $(GLEW_OBJ) $(MINIZIP_OBJ)

# Resources: the error dialog (200) and version info from mugen.rc, plus the
# assembly manifest that points at Elecbyte.MUGEN.libs (the original embeds it
# the same way, as RT_MANIFEST 1). windres takes a single .rc, so the manifest
# is appended to a copy of mugen.rc instead of being compiled separately --
# keeping it one .rc is what makes this build independent of cvtres /merge.
RC_OBJ  := $(OBJ_DIR)/res/mugen_all.res.o
ALL_RC  := $(OBJ_DIR)/res/mugen_all.rc

# --------------------------------------------------------------------- rules

.PHONY: all clean distclean check-toolchain check-libs help install
.DELETE_ON_ERROR:

all: $(TARGET)

# Static link. -Wl,--gc-sections drops the archive members nothing references;
# without it a static link of SDL_mixer pulls in every codec whether the game
# can use it or not.
# --exclude-all-symbols: SDL's main library asks the linker to export every
# symbol, which for a static link would export the entire engine (~600 names)
# and a few hundred KB of names with it. The engine exports nothing.
$(TARGET): $(ENGINE_OBJS) $(RC_OBJ) $(TP_LIBS)
	@echo "  LD      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -m32 -mwindows -o $@ $(ENGINE_OBJS) $(RC_OBJ) \
	    -Wl,--gc-sections \
	    -L$(LIB_DIR) -llua -lpng -lresample \
	    $(GLEW_OBJ) $(MINIZIP_OBJ) \
	    $(PKG_LIBS) $(SYS_LIBS) \
	    -mwindows

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

# --- libpng 1.2.38 ----------------------------------------------------------
# pnggccrd.c and pngvcrd.c compile to nothing unless PNG_USE_PNGGCCRD /
# PNG_USE_PNGVCRD is defined, i.e. libpng picks its portable C path.

$(OBJ_DIR)/libpng/%.o: $(TP_DIR)/libpng-1.2.38/%.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -I$(TP_DIR)/libpng-1.2.38 -I$(SDK_DIR)/include -c $< -o $@

$(PNG_LIB): $(PNG_OBJS)
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

# --- GLEW 1.5.4 -------------------------------------------------------------

$(GLEW_OBJ): $(TP_DIR)/glew-1.5.4/src/glew.c
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(TP_CFLAGS) -DGLEW_STATIC -I$(TP_DIR)/glew-1.5.4/include -c $< -o $@

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

ifeq ($(ICON_ICO),)
$(ALL_RC): $(SRC_DIR)/mugen.rc $(SRC_DIR)/mugen.manifest
else
$(ALL_RC): $(SRC_DIR)/mugen.rc $(SRC_DIR)/mugen.manifest $(ICON_ICO)
endif
	@echo "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)cat $(SRC_DIR)/mugen.rc > $@
	$(Q)printf '\n/* generated: the assembly manifest, RT_MANIFEST 1 */\n1 24 "%s"\n' \
	    "$(CURDIR)/$(SRC_DIR)/mugen.manifest" >> $@

$(RC_OBJ): $(ALL_RC)
	@echo "  WINDRES $<"
	$(Q)$(WINDRES) -I$(SRC_DIR) -I$(ICON_DIR) -i $< -o $@

# --- install ----------------------------------------------------------------
# Strips the executable and copies it into the game directory, where it stays.
# install/ is the shipped M.U.G.E.N 1.1b1 tree (git-ignored); the executable is
# named mugen-static.exe so it never shadows the original mugen.exe.

INSTALL_DIR ?= install
STRIPPED    := $(BUILD)/mugen-static-stripped.exe

$(STRIPPED): $(TARGET)
	@echo "  STRIP   $@"
	$(Q)$(STRIP) -o $@ $(TARGET)

install: $(STRIPPED)
	@echo "  INSTALL $(INSTALL_DIR)/mugen-static.exe"
	$(Q)mkdir -p $(INSTALL_DIR)
	$(Q)cp $(STRIPPED) $(INSTALL_DIR)/mugen-static.exe
	@echo "  done: run it from $(INSTALL_DIR) (no Elecbyte.MUGEN.libs needed)"

# --- housekeeping -----------------------------------------------------------

check-toolchain: check-libs
	@$(CC) -dumpmachine | grep -q '^i686-w64-mingw32' \
	  || { echo "error: $(CC) targets '$$($(CC) -dumpmachine)', need i686-w64-mingw32 (32-bit)"; exit 1; }
	@for t in $(AR) $(WINDRES) $(STRIP) $(PKG_CONFIG); do \
	  command -v $$t >/dev/null 2>&1 \
	    || { echo "error: $$t not found in PATH"; exit 1; }; \
	done
	@echo "toolchain ok: $$($(CC) --version | head -1) / $$($(CC) -dumpmachine)"
	@echo "python: $(if $(strip $(PYTHON3)),$$($(PYTHON3) --version),not found (only needed for MUGEN_GAME_DIR))"

# The static SDL stack must be present as archives, not just .pc files: a
# pkg-config hit with only a DLL behind it would link but not be static.
check-libs:
	@for pkg in $(STATIC_PKGS) zlib libmpg123; do \
	  $(PKG_CONFIG) --exists $$pkg \
	    || { echo "error: pkg-config cannot find '$$pkg'; install the $(1) static libraries"; exit 1; }; \
	done
	@for lib in SDL SDL_gfx SDL_mixer SDL_ttf z mpg123; do \
	  test -f "$(SDK_LIB_DIR)/lib$$lib.a" \
	    || { echo "error: $(SDK_LIB_DIR)/lib$$lib.a missing (need static archives, not DLLs)"; exit 1; }; \
	done
	@echo "static libraries ok: $(STATIC_PKGS) zlib mpg123"

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(ICON_DIR) $(TARGET) $(STRIPPED)

distclean:
	rm -rf $(BUILD)

help:
	@sed -n '2,33p' Makefile | sed 's/^# \{0,1\}//'

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
