#!/bin/sh
# Cross-build the suxty input plugin for aarch64 handhelds (ROCKNIX, spruce, Knulli, muOS ...) on an x86_64 host.
#
#   tools/build-aarch64.sh [SDL2_LIBDIR]
#
# Needs: aarch64-linux-gnu-gcc, SDL2 headers for the target (Debian/Ubuntu: libsdl2-dev:arm64, or the
# headers in /usr/aarch64-linux-gnu/include/SDL2), and a copy of the target's libSDL2-2.0.so.0 to link
# against.  suxty links against SDL2 only; GL/GLES entry points are resolved at run time.  Copy the
# library from the device if you have no arm64 sysroot, e.g.
#   scp device:/usr/lib/libSDL2-2.0.so.0 $SDL2_LIBDIR/
# Output: projects/unix/mupen64plus-input-suxty-aarch64.so  (install it as mupen64plus-input-suxty.so next to the other plugins)
# Note: the stock Ubuntu cross compiler emits glibc 2.38 symbols; for devices with an older glibc use toolchains/rg35xxsp/build.sh suxty.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
libdir=${1:-${SDL2_LIBDIR:-/usr/lib/aarch64-linux-gnu}}
incdir=${SDL2_INCDIR:-/usr/aarch64-linux-gnu/include/SDL2}
[ -e "$libdir/libSDL2-2.0.so.0" ] || { echo "no libSDL2-2.0.so.0 in $libdir (pass the directory as \$1)" >&2; exit 1; }
make -C "$here/projects/unix" all \
    CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 \
    SDL_CFLAGS="-I$incdir -D_REENTRANT" \
    SDL_LDLIBS="-L$libdir -l:libSDL2-2.0.so.0 -Wl,--allow-shlib-undefined -Wl,-rpath-link,$libdir"
