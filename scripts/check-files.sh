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
