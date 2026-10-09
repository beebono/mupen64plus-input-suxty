#/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# *   mupen64plus-input-suxty - Makefile                                    *
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
# Makefile for mupen64plus-input-suxty  (Linux only; native or cross)
#
#   native:        make all                      -> mupen64plus-input-suxty.so
#   aarch64 cross: make all CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 \
#                    SDL_CFLAGS="-I/usr/aarch64-linux-gnu/include/SDL2 -D_REENTRANT" \
#                    SDL_LDLIBS="-L<dir with libSDL2-2.0.so.0> -l:libSDL2-2.0.so.0"
#
# suxty needs SDL2 only: GL / GLES entry points are fetched at run time through
# SDL_GL_GetProcAddress, so neither GL headers nor libGLESv2/libEGL are needed to build.

UNAME ?= $(shell uname -s)
ifneq ("$(UNAME)","Linux")
  $(error mupen64plus-input-suxty targets Linux (found "$(UNAME)"))
endif

SRCDIR = ../../src
OBJDIR = _objp$(POSTFIX)

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

# installation: next to the other plugins
ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif
ifeq ($(LIBDIR),)
  LIBDIR := $(PREFIX)/lib
endif
ifeq ($(PLUGINDIR),)
  PLUGINDIR := $(LIBDIR)/mupen64plus
endif

ifeq ($(DEBUG),1)
  CFLAGS += -g
  INSTALL_STRIP_FLAG ?=
else
  INSTALL_STRIP_FLAG ?= -s
endif

# The plugin: only the Mupen64Plus plugin API is exported (-fvisibility=hidden + EXPORT), and the plugin binds its own
# symbols to itself (-Bsymbolic), so nothing in it can be interposed by, or interpose, another plugin or the front-end.
SOURCE = \
	$(SRCDIR)/audio.c \
	$(SRCDIR)/autosave.c \
	$(SRCDIR)/cfg.c \
	$(SRCDIR)/core_interface.c \
	$(SRCDIR)/debug.c \
	$(SRCDIR)/filewatch.c \
	$(SRCDIR)/foir_if.c \
	$(SRCDIR)/font_data.c \
	$(SRCDIR)/gl.c \
	$(SRCDIR)/inifile.c \
	$(SRCDIR)/inputfilter.c \
	$(SRCDIR)/menu.c \
	$(SRCDIR)/menu_av.c \
	$(SRCDIR)/menu_ctl.c \
	$(SRCDIR)/osal_files_unix.c \
	$(SRCDIR)/pad.c \
	$(SRCDIR)/padmap.c \
	$(SRCDIR)/plugin_api.c \
	$(SRCDIR)/remap.c \
	$(SRCDIR)/session.c \
	$(SRCDIR)/states.c \
	$(SRCDIR)/toast.c \
	$(SRCDIR)/ui.c \
	$(SRCDIR)/video.c

OBJECTS := $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(filter %.c, $(SOURCE)))
$(shell $(MKDIR) $(OBJDIR))

TARGET = mupen64plus-input-suxty$(POSTFIX).so
PLUGIN_CFLAGS  = -fPIC -fvisibility=hidden
PLUGIN_LDFLAGS = -shared -Wl,-Bsymbolic -Wl,--exclude-libs,ALL

targets:
	@echo "mupen64plus-input-suxty makefile."
	@echo "  Targets:"
	@echo "    all            == Build the plugin, mupen64plus-input-suxty.so"
	@echo "    check          == build and run the unit tests (N64 mapping model, config layers, foir interface, auto-save safety net, inotify watcher)"
	@echo "    clean          == remove object files and build products"
	@echo "    rebuild        == clean and re-build all"
	@echo "    install        == Install the plugin into PLUGINDIR"
	@echo "    uninstall      == Uninstall it"
	@echo "  Options:"
	@echo "    APIDIR=path    == path to find Mupen64Plus Core headers"
	@echo "    CROSS_COMPILE=prefix == toolchain prefix, e.g. aarch64-linux-gnu-"
	@echo "    SDL_CFLAGS / SDL_LDLIBS == SDL2 flags when pkg-config can't supply them (cross builds)"
	@echo "    OPTFLAGS=flags == compiler optimization (default: -O3)"
	@echo "    WARNFLAGS=flag == compiler warning levels (default: -Wall -Wextra)"
	@echo "    POSTFIX=name   == String added to the name of the the build (default: '')"
	@echo "    PREFIX=path    == install/uninstall prefix (default: /usr/local)"
	@echo "    PLUGINDIR=path == where the plugin is installed (default: PREFIX/lib/mupen64plus)"
	@echo "    DEBUG=1        == add debugging symbols"
	@echo "    V=1            == show verbose compiler output"

all: $(TARGET)

plugin: all

clean:
	$(RM) -r $(OBJDIR) $(TARGET) test_padmap test_cfg test_foir_if test_autosave fake_foir.so

rebuild: clean all

install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(PLUGINDIR)"
	$(INSTALL) -m 0644 $(INSTALL_STRIP_FLAG) $(TARGET) "$(DESTDIR)$(PLUGINDIR)/mupen64plus-input-suxty.so"

uninstall:
	$(RM) "$(DESTDIR)$(PLUGINDIR)/mupen64plus-input-suxty.so"

# build dependency files
CFLAGS += -MD -MP
-include $(OBJECTS:.o=.d)

# standard build rules
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@$(MKDIR) $(dir $@)
	$(COMPILE.c) $(PLUGIN_CFLAGS) -o $@ $<

$(TARGET): $(OBJECTS)
	$(Q_LD)$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) $^ $(LDLIBS) -o $@

# unit tests (need only the SDL2 headers): the N64 mapping model (src/padmap.c), the config layers and the migration of
# an old suxty.cfg (src/cfg.c), and foir discovery / option layering against a fake foir (src/foir_if.c)
check: test_padmap test_cfg test_foir_if fake_foir.so test_autosave
	./test_padmap
	./test_cfg
	./test_foir_if ./fake_foir.so
	./test_autosave

test_padmap: $(SRCDIR)/../tools/test_padmap.c $(SRCDIR)/padmap.c $(SRCDIR)/inifile.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) $(filter -I%,$(CFLAGS)) $(SDL_CFLAGS) -fsanitize=address,undefined -o $@ $^ $(SDL_LDLIBS) -lm

test_cfg: $(SRCDIR)/../tools/test_cfg.c $(SRCDIR)/cfg.c $(SRCDIR)/inifile.c $(SRCDIR)/pad.c $(SRCDIR)/osal_files_unix.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) $(filter -I%,$(CFLAGS)) $(SDL_CFLAGS) -fsanitize=address,undefined -o $@ $^ $(SDL_LDLIBS) -lm

test_foir_if: $(SRCDIR)/../tools/test_foir_if.c $(SRCDIR)/foir_if.c $(SRCDIR)/cfg.c $(SRCDIR)/inifile.c $(SRCDIR)/pad.c $(SRCDIR)/osal_files_unix.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) $(filter -I%,$(CFLAGS)) $(SDL_CFLAGS) -fsanitize=address,undefined -o $@ $^ $(SDL_LDLIBS) -lm -ldl

# the auto-save safety net and the inotify watcher (zlib is used by the test only, to write the files the plugin checks)
test_autosave: $(SRCDIR)/../tools/test_autosave.c $(SRCDIR)/autosave.c $(SRCDIR)/filewatch.c
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 $(WARNFLAGS) -I$(SRCDIR) -fsanitize=address,undefined -o $@ $^ -lz

fake_foir.so: $(SRCDIR)/../tools/fake_foir.c $(SRCDIR)/foir_suxty.h
	$(CC) -std=gnu11 -O1 -Wall -Wextra -fPIC -shared -I$(SRCDIR) -o $@ $<

.PHONY: all plugin clean rebuild install uninstall targets check
