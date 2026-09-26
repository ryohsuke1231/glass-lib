#!/bin/sh
# install-js.sh TSC SOURCE_DIR DATA_DIR — meson install script (demo/meson.build).
#
# Compiles the demos' TypeScript into DATA_DIR under the install prefix. The
# types are not checked here (--noCheck): that needs the generated Glass
# types and the @girs packages, which development has (npm run build) and a
# Flatpak build does not.
set -eu

tsc="$1"
src="$2"
dest="${MESON_INSTALL_DESTDIR_PREFIX}/$3"

"$tsc" --project "$src/tsconfig.json" --noCheck --outDir "$dest"
