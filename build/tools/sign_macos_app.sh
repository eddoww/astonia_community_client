#!/usr/bin/env bash

# Usage:
#   SIGN_IDENTITY="Apple Development: Your Name (TEAMID)" \
#   build/tools/sign_macos_app.sh [/path/to/Astonia.app]
#
# Defaults:
#   - SIGN_IDENTITY must be set in the environment
#   - App path defaults to ./distrib/Astonia.app

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_BUNDLE="${1:-"$PROJECT_ROOT/distrib/Astonia.app"}"
SIGN_IDENTITY="${SIGN_IDENTITY:-}"

if [[ -z "$SIGN_IDENTITY" ]]; then
  echo "ERROR: SIGN_IDENTITY is not set." >&2
  echo "       Example: SIGN_IDENTITY=\"Apple Development: Your Name (TEAMID)\" $0" >&2
  exit 1
fi

if [[ ! -d "$APP_BUNDLE" ]]; then
  echo "ERROR: App bundle not found: $APP_BUNDLE" >&2
  exit 1
fi

APP_CONTENTS="$APP_BUNDLE/Contents"
APP_MACOS="$APP_CONTENTS/MacOS"
APP_RESOURCES="$APP_CONTENTS/Resources"
APP_RES_BIN="$APP_RESOURCES/bin"

echo "Signing macOS app bundle..."
echo "  App:         $APP_BUNDLE"
echo "  Identity:    $SIGN_IDENTITY"

MACHO_FILES=()

# Everything under Resources/bin (moac, libastonia_net, SDL/codec libs, etc.)
if [[ -d "$APP_RES_BIN" ]]; then
  while IFS= read -r f; do
    MACHO_FILES+=("$f")
  done < <(find "$APP_RES_BIN" -type f ! -name "*.dSYM" -print)
fi

# Launcher and any other binaries in Contents/MacOS
if [[ -d "$APP_MACOS" ]]; then
  while IFS= read -r f; do
    MACHO_FILES+=("$f")
  done < <(find "$APP_MACOS" -type f ! -name "*.dSYM" -print)
fi

echo "==> Signing Mach-O files with identity: $SIGN_IDENTITY"

for f in "${MACHO_FILES[@]}"; do
  if file "$f" | grep -q "Mach-O"; then
    echo "==>   codesign: $f"
    codesign --force --options runtime --timestamp \
             --sign "$SIGN_IDENTITY" \
             "$f"
  fi
done

echo "==> Signing app bundle: $APP_BUNDLE"
codesign --force --options runtime --timestamp \
         --sign "$SIGN_IDENTITY" \
         "$APP_BUNDLE"

echo "==> Verifying code signature..."
codesign --verify --deep --strict --verbose=4 "$APP_BUNDLE"

echo "==> macOS app bundle SIGNED successfully."