#!/bin/bash
set -e

# Rust code formatting check (rustfmt only)
# Used by CI pipeline for parallel Rust formatting checks
# For local development, use check-format.sh instead (checks all languages)
# Exit code 0 = all formatted correctly, 1 = formatting issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Rust Code Format Checker"
echo "========================================"
echo ""

# Check if Rust code exists
if [ ! -d "astonia_net" ]; then
    echo "  - Rust directory not found, skipping"
    echo ""
    exit 0
fi

if ! command -v cargo >/dev/null 2>&1; then
    echo "WARNING: cargo not installed, skipping Rust formatting check"
    echo ""
    exit 0
fi

FAILED=0
LOG_DIR="build/logs"
mkdir -p "$LOG_DIR"

# ============================================================================
# Rust Formatting Check (rustfmt)
# ============================================================================
echo ">>> Checking Rust Code Formatting"

if cd astonia_net && cargo fmt --check > "../$LOG_DIR/rustfmt.log" 2>&1; then
    echo "  ✓ Rust code properly formatted"
    rm -f "../$LOG_DIR/rustfmt.log"
    cd "$PROJECT_ROOT"
else
    echo "ERROR: Rust code needs formatting"
    cat "../$LOG_DIR/rustfmt.log"
    echo "  → Full report: $LOG_DIR/rustfmt.log"
    echo "Run 'make lint' or 'cargo fmt' to fix"
    cd "$PROJECT_ROOT"
    FAILED=1
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "========================================"
    echo "  ✗ Rust Format Check FAILED"
    echo "========================================"
    exit 1
else
    echo "========================================"
    echo "  ✓ Rust Format Check PASSED"
    echo "========================================"
    exit 0
fi
