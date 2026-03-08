#!/usr/bin/env bash
# Bundle OCCT dylibs into the native addon directory for a self-contained release.
# Run after build:native, before electron-builder.
# Requires OCCT at $HOME/Libraries/opencascade/7.8.1

set -e

OCCT_LIB="${OCCT_LIB:-$HOME/Libraries/opencascade/7.8.1/lib}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OCCT_DEST="$PROJECT_ROOT/native/build/Release/occt"
NODE_FILE="$PROJECT_ROOT/native/build/Release/step_fixer_native.node"

if [[ ! -d "$OCCT_LIB" ]]; then
  echo "Error: OCCT lib not found at $OCCT_LIB"
  echo "Set OCCT_LIB or install OCCT to \$HOME/Libraries/opencascade/7.8.1"
  exit 1
fi

if [[ ! -f "$NODE_FILE" ]]; then
  echo "Error: Native addon not built. Run: npm run build:native"
  exit 1
fi

echo "Bundling OCCT from $OCCT_LIB into $OCCT_DEST"

rm -rf "$OCCT_DEST"
mkdir -p "$OCCT_DEST"

# Copy all dylibs (dereference symlinks so we get real files with correct names)
for f in "$OCCT_LIB"/*.dylib; do
  [[ -e "$f" ]] || continue
  cp -L "$f" "$OCCT_DEST/$(basename "$f")"
done

echo "Copied $(ls -1 "$OCCT_DEST" | wc -l) dylibs"

# Replace the .node's rpath so it finds OCCT in the bundle
# The loader uses @loader_path = dir containing the .node; we put occt/ next to it
OLD_RPATH="$OCCT_LIB"
NEW_RPATH="@loader_path/occt"

install_name_tool -rpath "$OLD_RPATH" "$NEW_RPATH" "$NODE_FILE"

echo "Updated step_fixer_native.node rpath: $OLD_RPATH -> $NEW_RPATH"
echo "OCCT bundle complete."
