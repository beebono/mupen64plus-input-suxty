#/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# *   mupen64plus-ui-suxty - Makefile                                       *
# *   Mupen64Plus homepage: https://mupen64plus.org/                        *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU General Public License as published by  *
# *   the Free Software Foundation; either version 2 of the License, or     *
# *   (at your option) any later version.                                   *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU General Public License for more details.                          *
# *                                                                         *
# *   You should have received a copy of the GNU General Public License     *
# *   along with this program; if not, write to the                         *
# *   Free Software Foundation, Inc.,                                       *
# *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
# * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
# Makefile for mupen64plus-ui-suxty  (Linux only; native or cross)
#
#   native:        make all
#   aarch64 cross: make all CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 \
#                    SDL_CFLAGS="-I/usr/aarch64-linux-gnu/include/SDL2 -D_REENTRANT" \
#                    SDL_LDLIBS="-L<dir with libSDL2-2.0.so.0> -l:libSDL2-2.0.so.0"
#
# suxty needs SDL2 only: GL / GLES entry points are fetched at run time through
# SDL_GL_GetProcAddress, so neither GL headers nor libGLESv2/libEGL are needed to build.

UNAME ?= $(shell uname -s)
ifneq ("$(UNAME)","Linux")
  $(error mupen64plus-ui-suxty targets Linux (found "$(UNAME)"))
endif

SRCDIR = ../../src
OBJDIR = _obj$(POSTFIX)

OPTFLAGS ?= -O3
WARNFLAGS ?= -Wall -Wextra

CFLAGS += -std=gnu11 -D_GNU_SOURCE $(OPTFLAGS) $(WARNFLAGS) -I$(SRCDIR) -pthread
LDLIBS += -pthread -ldl -lm

# SDL2: pkg-config unless SDL_CFLAGS and SDL_LDLIBS are given (needed for cross builds without a target pkg-config)
ifeq ($(origin SDL_CFLAGS) $(origin SDL_LDLIBS), undefined undefined)
  ifeq ($(origin PKG_CONFIG), undefined)
    PKG_CONFIG = $(if $(shell which $(CROSS_COMPILE)pkg-config 2>/dev/null),$(CROSS_COMPILE)pkg-config,pkg-config)
  endif
  ifeq ($(shell $(PKG_CONFIG) --modversion sdl2 2>/dev/null),)
    $(error No SDL2 development libraries found! Set SDL_CFLAGS and SDL_LDLIBS, or PKG_CONFIG)
  endif
  SDL_CFLAGS += $(shell $(PKG_CONFIG) --cflags sdl2)
  SDL_LDLIBS += $(shell $(PKG_CONFIG) --libs sdl2)
endif
CFLAGS += $(SDL_CFLAGS)
LDLIBS += $(SDL_LDLIBS)

ifeq ($(BIG_ENDIAN), 1)
  CFLAGS += -DM64P_BIG_ENDIAN
endif

# set mupen64plus core API header path
ifneq ("$(APIDIR)","")
  CFLAGS += "-I$(APIDIR)"
else
  TRYDIR = ../../../mupen64plus-core/src/api
  ifneq ("$(wildcard $(TRYDIR)/m64p_types.h)","")
    CFLAGS += -I$(TRYDIR)
  else
    TRYDIR = /usr/local/include/mupen64plus
    ifneq ("$(wildcard $(TRYDIR)/m64p_types.h)","")
      CFLAGS += -I$(TRYDIR)
    else
      TRYDIR = /usr/include/mupen64plus
      ifneq ("$(wildcard $(TRYDIR)/m64p_types.h)","")
        CFLAGS += -I$(TRYDIR)
      else
        $(error Mupen64Plus API header files not found! Use makefile parameter APIDIR to force a location.)
      endif
    endif
  endif
endif

# reduced compile output when running make without V=1
ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	Q_CC  = @echo '    CC  '$@;
	Q_LD  = @echo '    LD  '$@;
endif
endif

CC        = $(CROSS_COMPILE)gcc
RM       ?= rm -f
INSTALL  ?= install
MKDIR    ?= mkdir -p
COMPILE.c = $(Q_CC)$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
LINK.o = $(Q_LD)$(CC) $(CFLAGS) $(LDFLAGS) $(TARGET_ARCH)

# PIE handling, as in the other Mupen64Plus makefiles
ifndef PIE
  ifeq ($(CC),$(CROSS_COMPILE)gcc)
    PIE_DEFAULT = $(shell $(CC) -v 2>&1 | grep enable-default-pie)
    ifeq ($(PIE_DEFAULT),)
      PIE = 1
    endif
  endif
endif
ifdef PIE
  ifeq ($(PIE), 1)
    CFLAGS += -fPIE
    LDFLAGS += -pie
  else
    ifeq ($(PIE), 0)
      CFLAGS += -fno-PIE
      LDFLAGS += -no-pie
    else
      $(error Invalid PIE value.)
    endif
  endif
endif

# export the SDL joystick overrides of src/inputfilter.c to the plugins
LDFLAGS += -Wl,--dynamic-list=$(CURDIR)/suxty.exports

# installation
ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif
ifeq ($(LIBDIR),)
  LIBDIR := $(PREFIX)/lib
endif
ifeq ($(BINDIR),)
  BINDIR := $(PREFIX)/bin
endif

# default search locations (same meaning as in mupen64plus-ui-console)
ifeq ($(PLUGINDIR),)
  CFLAGS += '-DPLUGINDIR="$(LIBDIR)/mupen64plus"'
else
  CFLAGS += '-DPLUGINDIR="$(PLUGINDIR)"'
endif
# note: COREDIR _must_ end in a slash if you want it to work; not necessary for the others
ifeq ($(COREDIR),)
  CFLAGS += '-DCOREDIR="$(LIBDIR)/"'
else
  CFLAGS += '-DCOREDIR="$(COREDIR)"'
endif
ifneq ($(SHAREDIR),)
  CFLAGS += '-DSHAREDIR="$(SHAREDIR)"'
endif
ifeq ($(DEBUG),1)
  CFLAGS += -g
  INSTALL_STRIP_FLAG ?=
else
  INSTALL_STRIP_FLAG ?= -s
endif

SOURCE = \
	$(SRCDIR)/cfg.c \
	$(SRCDIR)/core_interface.c \
	$(SRCDIR)/debug.c \
	$(SRCDIR)/font_data.c \
	$(SRCDIR)/gl.c \
	$(SRCDIR)/inifile.c \
	$(SRCDIR)/inputfilter.c \
	$(SRCDIR)/main.c \
	$(SRCDIR)/menu.c \
	$(SRCDIR)/menu_ctl.c \
	$(SRCDIR)/menu_prof.c \
	$(SRCDIR)/osal_dynamiclib_unix.c \
	$(SRCDIR)/osal_files_unix.c \
	$(SRCDIR)/pad.c \
	$(SRCDIR)/padmap.c \
	$(SRCDIR)/plugin.c \
	$(SRCDIR)/profile.c \
	$(SRCDIR)/remap.c \
	$(SRCDIR)/session.c \
	$(SRCDIR)/states.c \
	$(SRCDIR)/toast.c \
	$(SRCDIR)/ui.c \
	$(SRCDIR)/video.c

OBJECTS := $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(filter %.c, $(SOURCE)))
OBJDIRS = $(dir $(OBJECTS))
$(shell $(MKDIR) $(OBJDIRS))

TARGET = suxty$(POSTFIX)

# --- SPIKE: suxty as an input plugin (mupen64plus-input-suxty.so), see src/plugin_api.c --------------------------------
# Same modules, built with -DSUXTY_PLUGIN into their own object directory; main.c (the front-end) and plugin.c (plugin
# loading) are not part of it.  Only the Mupen64Plus plugin API is exported (-fvisibility=hidden + EXPORT).
PLUGIN_TARGET  = mupen64plus-input-suxty$(POSTFIX).so
PLUGIN_OBJDIR  = _obj-plugin$(POSTFIX)
PLUGIN_SOURCE  = $(filter-out $(SRCDIR)/main.c $(SRCDIR)/plugin.c $(SRCDIR)/inputfilter.c,$(SOURCE)) $(SRCDIR)/inputfilter.c $(SRCDIR)/plugin_api.c
PLUGIN_OBJECTS = $(patsubst $(SRCDIR)/%.c, $(PLUGIN_OBJDIR)/%.o, $(PLUGIN_SOURCE))
$(shell $(MKDIR) $(PLUGIN_OBJDIR))
PLUGIN_CFLAGS  = -DSUXTY_PLUGIN -fPIC -fvisibility=hidden
PLUGIN_LDFLAGS = -shared -Wl,-Bsymbolic -Wl,--exclude-libs,ALL

targets:
	@echo "mupen64plus-ui-suxty makefile."
	@echo "  Targets:"
	@echo "    all            == Build the suxty front-end application"
	@echo "    check          == build and run the unit tests (N64 mapping model, config layers)"
	@echo "    clean          == remove object files and build products"
	@echo "    rebuild        == clean and re-build all"
	@echo "    install        == Install suxty"
	@echo "    uninstall      == Uninstall suxty"
	@echo "  Options:"
	@echo "    COREDIR=path   == default path to search for Mupen64Plus Core (must end with slash)"
	@echo "    PLUGINDIR=path == default path to search for plugins"
	@echo "    SHAREDIR=path  == default path to search for shared data files"
	@echo "    APIDIR=path    == path to find Mupen64Plus Core headers"
	@echo "    CROSS_COMPILE=prefix == toolchain prefix, e.g. aarch64-linux-gnu-"
	@echo "    SDL_CFLAGS / SDL_LDLIBS == SDL2 flags when pkg-config can't supply them (cross builds)"
	@echo "    OPTFLAGS=flags == compiler optimization (default: -O3)"
	@echo "    WARNFLAGS=flag == compiler warning levels (default: -Wall -Wextra)"
	@echo "    PIE=(1|0)      == Force enable/disable of position independent executables"
	@echo "    POSTFIX=name   == String added to the name of the the build (default: '')"
	@echo "    PREFIX=path    == install/uninstall prefix (default: /usr/local)"
	@echo "    DEBUG=1        == add debugging symbols"
	@echo "    V=1            == show verbose compiler output"

all: $(TARGET)

plugin: $(PLUGIN_TARGET)

clean:
	$(RM) -r $(OBJDIR) $(PLUGIN_OBJDIR) $(TARGET) $(PLUGIN_TARGET) test_padmap test_cfg

rebuild: clean all

install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 $(INSTALL_STRIP_FLAG) $(TARGET) "$(DESTDIR)$(BINDIR)"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/$(TARGET)"

# build dependency files
CFLAGS += -MD -MP
-include $(OBJECTS:.o=.d)

# standard build rules
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@$(MKDIR) $(dir $@)
	$(COMPILE.c) -o $@ $<

$(TARGET): $(OBJECTS)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(PLUGIN_OBJDIR)/%.o: $(SRCDIR)/%.c
	@$(MKDIR) $(dir $@)
	$(COMPILE.c) $(PLUGIN_CFLAGS) -o $@ $<

$(PLUGIN_TARGET): $(PLUGIN_OBJECTS)
	$(Q_LD)$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) $^ $(LDLIBS) -o $@

-include $(PLUGIN_OBJECTS:.o=.d)

# unit tests of the N64 mapping model (src/padmap.c); needs only the SDL2 headers
check: test_padmap test_cfg
	./test_padmap
	./test_cfg

test_padmap: $(SRCDIR)/../tools/test_padmap.c $(SRCDIR)/padmap.c $(SRCDIR)/inifile.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) $(filter -I%,$(CFLAGS)) $(SDL_CFLAGS) -fsanitize=address,undefined -o $@ $^ $(SDL_LDLIBS) -lm

test_cfg: $(SRCDIR)/../tools/test_cfg.c $(SRCDIR)/cfg.c $(SRCDIR)/inifile.c $(SRCDIR)/pad.c $(SRCDIR)/osal_files_unix.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) $(filter -I%,$(CFLAGS)) $(SDL_CFLAGS) -fsanitize=address,undefined -o $@ $^ $(SDL_LDLIBS) -lm

.PHONY: all plugin clean install uninstall targets check
