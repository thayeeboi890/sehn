#!/bin/sh
set -eu
src="$MESON_SOURCE_ROOT/share/fonts/yudit.ttf"
dest="${MESON_INSTALL_DESTDIR_PREFIX}/$1/yudit.ttf"
[ -f "$dest" ] || install -Dm644 "$src" "$dest"
