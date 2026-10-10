#!/bin/sh
# usage: tools/build-aarch64.sh [SDL2_LIBDIR]  (a copy of the target's libSDL2-2.0.so.0 to link against, e.g. from the device)

set -e
here=$(cd "$(dirname "$0")/.." && pwd)
libdir=${1:-${SDL2_LIBDIR:-/usr/lib/aarch64-linux-gnu}}
incdir=${SDL2_INCDIR:-/usr/aarch64-linux-gnu/include/SDL2}
[ -e "$libdir/libSDL2-2.0.so.0" ] || { echo "no libSDL2-2.0.so.0 in $libdir (pass the directory as \$1)" >&2; exit 1; }
make -C "$here/projects/unix" all \
    CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 \
    SDL_CFLAGS="-I$incdir -D_REENTRANT" \
    SDL_LDLIBS="-L$libdir -l:libSDL2-2.0.so.0 -Wl,--allow-shlib-undefined -Wl,-rpath-link,$libdir"
