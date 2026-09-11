#!/bin/bash
set -e

# Stage the self-contained mod SDK in mod-sdk/.
#
# Single source of truth for what ships in the mod-sdk CI artifacts and the
# release's mod-sdk.zip (build-and-quality.yml and release.yml both run this,
# locally: `make mod-sdk-stage`). Keep the file list in sync with the include
# graph of src/amod/amod.h — the compile check below fails when it drifts.
#
# Staged layout (see build/mod-sdk/README.md for the consumer view):
#   README.md                   usage instructions
#   src/...                     client API headers, mirroring the repo's src/
#   include/SDL3/SDL_keycode.h  shim for building without SDL3 dev headers
#   lib/moac.a, lib/moac.lib    Windows import libraries (when built)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

DEST="mod-sdk"

rm -rf "$DEST"
mkdir -p "$DEST/src/amod" "$DEST/src/game"

cp build/mod-sdk/README.md "$DEST/"
cp -r build/mod-sdk/include "$DEST/include"
cp src/astonia.h src/dll.h "$DEST/src/"
cp src/amod/amod.h src/amod/amod_structs.h src/amod/amod_options.h "$DEST/src/amod/"
cp src/game/memory.h "$DEST/src/game/"

# Import libraries exist only after a Windows build.
for lib in lib/moac.a lib/moac.lib; do
    if [ -f "$lib" ]; then
        mkdir -p "$DEST/lib"
        cp "$lib" "$DEST/lib/"
    fi
done

# Self-containedness check: the staged headers must compile on their own,
# exactly as an out-of-tree mod sees them (no repo src/, no SDL3 or mimalloc
# dev headers). This is what catches a header added to the include graph but
# forgotten above.
CHECK_CC=""
for cand in "${CC:-}" cc gcc clang; do
    if [ -n "$cand" ] && command -v "$cand" >/dev/null 2>&1; then
        CHECK_CC="$cand"
        break
    fi
done

if [ -n "$CHECK_CC" ]; then
    TMP_DIR="$(mktemp -d)"
    trap 'rm -rf "$TMP_DIR"' EXIT
    printf '#include "amod/amod.h"\n' > "$TMP_DIR/sdk_check.c"
    "$CHECK_CC" -fsyntax-only -DUSE_MIMALLOC=0 -I"$DEST/src" -I"$DEST/include" "$TMP_DIR/sdk_check.c"
    echo "Staged SDK headers compile standalone ($CHECK_CC)"
else
    echo "WARNING: no C compiler found, skipped the standalone compile check"
fi

echo "Mod SDK staged in $DEST/:"
find "$DEST" -type f | sort
