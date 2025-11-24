#!/bin/bash
set -e

# Format checking script for C and Rust code
# Used by both CI pipeline and local development (make format-check)
# Exit code 0 = all formatted correctly, 1 = formatting issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Code Format Checker"
echo "========================================"
echo ""

FAILED=0
LOG_DIR="build/logs"
mkdir -p "$LOG_DIR"

# ============================================================================
# C Code Formatting Check (clang-format)
# ============================================================================
echo ">>> Checking C Code Formatting"

# Detect clang-format
CLANG_FORMAT=$(which clang-format-21 2>/dev/null || which clang-format 2>/dev/null || echo "")
if [ -z "$CLANG_FORMAT" ]; then
    echo "ERROR: clang-format not found"
    echo "Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install clang-format"
    echo "  Arch: sudo pacman -S clang"
    echo "  macOS: brew install clang-format"
    exit 1
fi

CLANG_FORMAT_VERSION=$($CLANG_FORMAT --version 2>/dev/null | grep -oP 'version \K[0-9]+' || echo "0")
if [ "$CLANG_FORMAT_VERSION" != "21" ]; then
    echo "WARNING: Using clang-format version $CLANG_FORMAT_VERSION"
    echo "         CI pipeline uses version 21 - results may differ"
fi

# Check all C/H files
rm -f "$LOG_DIR/clang-format.log"
C_FORMAT_FAILED=0

for file in $(find src -type f \( -name "*.c" -o -name "*.h" \)); do
    if ! $CLANG_FORMAT --dry-run -Werror --style=file "$file" 2>/dev/null; then
        if [ $C_FORMAT_FAILED -eq 0 ]; then
            echo "ERROR: The following files need formatting:" | tee "$LOG_DIR/clang-format.log"
        fi
        echo "  - $file" | tee -a "$LOG_DIR/clang-format.log"
        C_FORMAT_FAILED=1
    fi
done

if [ $C_FORMAT_FAILED -eq 1 ]; then
    echo "" | tee -a "$LOG_DIR/clang-format.log"
    echo "Run 'make lint' or 'scripts/format-code.sh' to fix" | tee -a "$LOG_DIR/clang-format.log"
    echo "  → Full report: $LOG_DIR/clang-format.log"
    FAILED=1
else
    echo "  ✓ All C files properly formatted"
    rm -f "$LOG_DIR/clang-format.log"
fi
echo ""

# ============================================================================
# Rust Code Formatting Check (rustfmt)
# ============================================================================
echo ">>> Checking Rust Code Formatting"

if [ ! -d "astonia_net" ]; then
    echo "  - Rust directory not found, skipping"
    echo ""
elif ! command -v cargo >/dev/null 2>&1; then
    echo "WARNING: cargo not installed, skipping Rust checks"
    echo ""
else
    if cd astonia_net && cargo fmt --check > "../$LOG_DIR/rustfmt.log" 2>&1; then
        echo "  ✓ Rust code properly formatted"
        rm -f "../$LOG_DIR/rustfmt.log"
        cd "$PROJECT_ROOT"
    else
        echo "ERROR: Rust code needs formatting"
        cat "../$LOG_DIR/rustfmt.log"
        echo "  → Full report: $LOG_DIR/rustfmt.log"
        echo "Run 'make lint' or 'scripts/format-code.sh' to fix"
        cd "$PROJECT_ROOT"
        FAILED=1
    fi
    echo ""
fi

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "========================================"
    echo "  ✗ Format Check FAILED"
    echo "========================================"
    exit 1
else
    echo "========================================"
    echo "  ✓ Format Check PASSED"
    echo "========================================"
    exit 0
fi
