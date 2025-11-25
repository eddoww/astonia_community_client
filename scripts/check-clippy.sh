#!/bin/bash
set -e

# Rust linting check (clippy only)
# Used by CI pipeline for parallel Rust linting checks
# For local development, use check-lint.sh instead (checks all languages)
# Exit code 0 = no issues, 1 = issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Rust Linting (clippy)"
echo "========================================"
echo ""

# Check if Rust code exists
if [ ! -d "astonia_net" ]; then
    echo "  - Rust directory not found, skipping"
    echo ""
    exit 0
fi

if ! command -v cargo >/dev/null 2>&1; then
    echo "WARNING: cargo not installed, skipping Rust linting"
    echo ""
    exit 0
fi

FAILED=0
LOG_DIR="build/logs"
mkdir -p "$LOG_DIR"

# ============================================================================
# Rust Linting (clippy)
# ============================================================================
echo ">>> Running Rust Linting (clippy)"

if cd astonia_net && cargo clippy -- -D warnings > "../$LOG_DIR/clippy.log" 2>&1; then
    echo "  ✓ No clippy warnings"
    rm -f "../$LOG_DIR/clippy.log"
    cd "$PROJECT_ROOT"
else
    echo "  ✗ clippy found issues:"
    cat "../$LOG_DIR/clippy.log"
    echo "  → Full report: $LOG_DIR/clippy.log"
    cd "$PROJECT_ROOT"
    FAILED=1
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "========================================"
    echo "  ✗ Rust Linting FAILED"
    echo "========================================"
    exit 1
else
    echo "========================================"
    echo "  ✓ Rust Linting PASSED"
    echo "========================================"
    exit 0
fi
