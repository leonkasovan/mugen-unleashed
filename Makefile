# mugen-unleashed -- GNU Make + GCC (MinGW-w64, 32-bit) build.
#
# This is an alternative to the CMake/MSVC build in CMakeLists.txt and produces
# the same executable from the same sources: the engine in src/, the libraries
# in third_party/ built from their original sources, and import libraries for
# the game's DLLs generated from the export lists in third_party/sdk/def/.
#
# The target is a 32-bit Windows GUI executable. It must be built with an
# i686-w64-mingw32 toolchain (x86_64-w64-mingw32 will not do: the engine is a
# 32-bit binary reconstruction, its struct layouts and pointers are 4 bytes).
#
#   make              build $(BUILD)/mugen.exe
#   make -j8          parallel build
#   make V=1          show the full command lines
#   make clean        remove the object files of $(BUILD)
#   make distclean    remove $(BUILD) entirely
#   make check-toolchain   verify the toolchain is 32-bit MinGW
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
DLLTOOL  ?= dlltool
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
IMPLIB_DIR:= $(BUILD)/implib
ICON_DIR  := $(BUILD)/icon
TARGET    := $(BUILD)/mugen.exe

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
ENGINE_DEFS := -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE

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

CPPFLAGS += -I$(SRC_DIR) \
            -I$(SDK_DIR)/include \
            -I$(SDK_DIR)/include/SDL \
            -I$(TP_DIR)/lua-5.1.4/src \
            -I$(TP_DIR)/libpng-1.2.38 \
            -I$(TP_DIR)/glew-1.5.4/include \
            -I$(TP_DIR)/minizip-0.15

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

# Import libraries for the DLLs the game ships with (Elecbyte.MUGEN.libs).
# Built from the .def files with the export lists of the original DLLs, so the
# imports match the original binary exactly -- same names, same order.
MUGEN_DLLS := SDL SDL_mixer SDL_ttf SDL_gfx zlib1 libresample pthreadVC2

# Windows system libraries, as in target_link_libraries() in CMakeLists.txt.
SYS_LIBS   := -lopengl32 -lglu32 -lwinmm -lshell32 -luser32 -lgdi32

# ------------------------------------------------------------------- sources

# The engine, one file per module of the original (see docs/ANALYSIS.md).
ENGINE_SRCS := $(wildcard $(SRC_DIR)/*.c)
ENGINE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/src/%.o,$(ENGINE_SRCS))

# Third-party libraries, built from source exactly as CMakeLists.txt and
# cmake/ThirdParty.cmake build them.
LUA_SRCS := $(wildcard $(TP_DIR)/lua-5.1.4/src/*.c)
PNG_SRCS := $(filter-out %/pngtest.c,$(wildcard $(TP_DIR)/libpng-1.2.38/png*.c))

LUA_OBJS := $(patsubst $(TP_DIR)/lua-5.1.4/src/%.c,$(OBJ_DIR)/lua/%.o,$(LUA_SRCS))
PNG_OBJS := $(patsubst $(TP_DIR)/libpng-1.2.38/%.c,$(OBJ_DIR)/libpng/%.o,$(PNG_SRCS))
GLEW_OBJ := $(OBJ_DIR)/glew/glew.o
MINIZIP_OBJ := $(OBJ_DIR)/minizip/unzip.o

LUA_LIB := $(LIB_DIR)/liblua.a
PNG_LIB := $(LIB_DIR)/libpng.a
TP_LIBS := $(LUA_LIB) $(PNG_LIB) $(GLEW_OBJ) $(MINIZIP_OBJ)

IMPLIBS := $(addprefix $(IMPLIB_DIR)/lib,$(addsuffix .a,$(MUGEN_DLLS)))

# Resources: the error dialog (200) and version info from mugen.rc, plus the
# assembly manifest that points at Elecbyte.MUGEN.libs (the original embeds it
# the same way, as RT_MANIFEST 1). windres takes a single .rc, so the manifest
# is appended to a copy of mugen.rc instead of being compiled separately --
# keeping it one .rc is what makes this build independent of cvtres /merge.
RC_OBJ  := $(OBJ_DIR)/res/mugen_all.res.o
ALL_RC  := $(OBJ_DIR)/res/mugen_all.rc

# --------------------------------------------------------------------- rules

.PHONY: all clean distclean check-toolchain help
.DELETE_ON_ERROR:

all: $(TARGET)

$(TARGET): $(ENGINE_OBJS) $(RC_OBJ) $(TP_LIBS) $(IMPLIBS)
	@echo "  LD      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -m32 -mwindows -o $@ $(ENGINE_OBJS) $(RC_OBJ) \
	    -L$(LIB_DIR) -llua -lpng \
	    $(GLEW_OBJ) $(MINIZIP_OBJ) \
	    $(IMPLIBS) $(SYS_LIBS)

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

# --- import libraries of the game's DLLs ------------------------------------

$(IMPLIB_DIR)/lib%.a: $(SDK_DIR)/def/%.def
	@echo "  DLLTOOL $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(DLLTOOL) -d $< -l $@

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

# --- housekeeping -----------------------------------------------------------

check-toolchain:
	@$(CC) -dumpmachine | grep -q '^i686-w64-mingw32' \
	  || { echo "error: $(CC) targets '$$($(CC) -dumpmachine)', need i686-w64-mingw32 (32-bit)"; exit 1; }
	@for t in $(AR) $(WINDRES) $(DLLTOOL); do \
	  command -v $$t >/dev/null 2>&1 \
	    || { echo "error: $$t not found in PATH"; exit 1; }; \
	done
	@echo "toolchain ok: $$($(CC) --version | head -1) / $$($(CC) -dumpmachine)"
	@echo "python: $(if $(strip $(PYTHON3)),$$($(PYTHON3) --version),not found (only needed for MUGEN_GAME_DIR))"

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(IMPLIB_DIR) $(ICON_DIR) $(TARGET)

distclean:
	rm -rf $(BUILD)

help:
	@sed -n '2,33p' Makefile | sed 's/^# \{0,1\}//'

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
