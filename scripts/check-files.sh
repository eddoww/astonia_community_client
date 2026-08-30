#!/bin/bash
set -e

# File consistency checks: line endings, encoding, etc.
# Used by both CI pipeline and local development
# Exit code 0 = all checks passed, 1 = issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  File Consistency Checks"
echo "========================================"
echo ""

FAILED=0
LOG_DIR="build/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR/file-consistency.log"

# ============================================================================
# Check Line Endings (LF only)
# ============================================================================
echo ">>> Checking Line Endings"

if git ls-files -z '*.c' '*.h' '*.rs' '*.toml' '*.md' '*.sh' 'Makefile*' 2>/dev/null | \
   xargs -0 file 2>/dev/null | grep -i "CRLF" >/dev/null; then
    echo "ERROR: Found files with CRLF line endings:" | tee "$LOG_DIR/file-consistency.log"
    git ls-files -z '*.c' '*.h' '*.rs' '*.toml' '*.md' '*.sh' 'Makefile*' 2>/dev/null | \
       xargs -0 file 2>/dev/null | grep -i "CRLF" | tee -a "$LOG_DIR/file-consistency.log"
    echo "Fix with: dos2unix <file>" | tee -a "$LOG_DIR/file-consistency.log"
    FAILED=1
else
    echo "  ✓ All text files use LF line endings"
fi
echo ""

# ============================================================================
# Check File Encoding (UTF-8)
# ============================================================================
echo ">>> Checking File Encoding"

if git ls-files -z '*.c' '*.h' '*.rs' '*.toml' '*.md' 2>/dev/null | \
   xargs -0 file -i 2>/dev/null | grep -v "charset=utf-8\|charset=us-ascii" >/dev/null; then
    if [ $FAILED -eq 0 ]; then
        echo "ERROR: Found files with non-UTF-8 encoding:" | tee "$LOG_DIR/file-consistency.log"
    else
        echo "ERROR: Found files with non-UTF-8 encoding:" | tee -a "$LOG_DIR/file-consistency.log"
    fi
    git ls-files -z '*.c' '*.h' '*.rs' '*.toml' '*.md' 2>/dev/null | \
       xargs -0 file -i 2>/dev/null | grep -v "charset=utf-8\|charset=us-ascii" | tee -a "$LOG_DIR/file-consistency.log"
    FAILED=1
else
    echo "  ✓ All text files use UTF-8 encoding"
fi
echo ""

# ============================================================================
# Check Tabs vs Spaces Consistency (informational)
# ============================================================================
echo ">>> Checking Tabs/Spaces Consistency"

# C files should use tabs (based on existing style)
TABS_IN_C=$(git ls-files 'src/**/*.c' 2>/dev/null | head -5 | xargs grep -l $'^\t' 2>/dev/null | wc -l || echo "0")
if [ "$TABS_IN_C" -gt 0 ]; then
    echo "  → C code uses tabs (consistent with project style)"
    # Check for inconsistencies (spaces where tabs expected)
    if git ls-files 'src/**/*.c' 2>/dev/null | xargs grep -l '^    ' 2>/dev/null | head -1 >/dev/null; then
        echo "  ⚠ Some C files use leading spaces (clang-format will handle this)"
    fi
else
    echo "  → Insufficient C files to determine style"
fi
echo ""

# ============================================================================
# Check Build Source Lists (filesystem vs Makefiles vs build.zig)
# ============================================================================
echo ">>> Checking Build Source Lists"

# Standalone tools built as separate targets, not linked into the game exe
BUILD_EXCLUDE=" src/helper/anicopy.c src/helper/convert.c src/sdl/sdl_test.c "

BUILD_SRC_ERRORS=""

# Every game source on disk must be referenced by build.zig and the
# platform Makefiles (platform-suffixed files only by their own platform).
while IFS= read -r f; do
    case "$BUILD_EXCLUDE" in *" $f "*) continue ;; esac
    obj="${f%.c}.o"
    targets="zig linux windows macos"
    case "$f" in
        *_windows.c) targets="zig windows" ;;
        *_macos.c)   targets="zig macos" ;;
        *_linux.c)   targets="zig linux" ;;
    esac
    for t in $targets; do
        case "$t" in
            zig) grep -q "\"$f\"" build/build.zig || \
                BUILD_SRC_ERRORS="$BUILD_SRC_ERRORS  $f missing from build/build.zig\n" ;;
            *)   grep -q -e "$f" -e "$obj" "build/make/Makefile.$t" || \
                BUILD_SRC_ERRORS="$BUILD_SRC_ERRORS  $f missing from build/make/Makefile.$t\n" ;;
        esac
    done
done < <(git ls-files 'src/gui/*.c' 'src/client/*.c' 'src/game/*.c' 'src/sdl/*.c' 'src/modder/*.c' 'src/helper/*.c')

# Every source referenced by the build files must exist on disk (no dead rules)
for ref in $(grep -oE '"src/[A-Za-z0-9_/.]+\.c"' build/build.zig | tr -d '"' | sort -u); do
    [ -f "$ref" ] || BUILD_SRC_ERRORS="$BUILD_SRC_ERRORS  build/build.zig references missing file $ref\n"
done
for mk in build/make/Makefile.linux build/make/Makefile.windows build/make/Makefile.macos; do
    for ref in $(grep -oE 'src/[A-Za-z0-9_/]+\.[co]\b' "$mk" | sed 's/\.o$/.c/' | sort -u); do
        [ -f "${ref%.c}.rc" ] && continue  # windres objects (resource.rc -> resource.o)
        [ -f "$ref" ] || BUILD_SRC_ERRORS="$BUILD_SRC_ERRORS  $mk references missing file $ref\n"
    done
done

if [ -n "$BUILD_SRC_ERRORS" ]; then
    echo "ERROR: Build source lists are out of sync:" | tee -a "$LOG_DIR/file-consistency.log"
    printf "%b" "$BUILD_SRC_ERRORS" | tee -a "$LOG_DIR/file-consistency.log"
    FAILED=1
else
    echo "  ✓ Makefiles and build.zig agree on game sources"
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "  → Full report: $LOG_DIR/file-consistency.log"
    echo ""
    echo "========================================"
    echo "  ✗ File Checks FAILED"
    echo "========================================"
    exit 1
else
    rm -f "$LOG_DIR/file-consistency.log"
    echo "========================================"
    echo "  ✓ File Checks PASSED"
    echo "========================================"
    exit 0
fi
