#!/bin/bash
set -e

# Linting check script for all languages (C and Rust)
# Used by local development (make check-lint)
# For CI pipeline, use check-static-analysis.sh and check-clippy.sh separately
# Exit code 0 = no issues, 1 = issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Code Linting Checker (All Languages)"
echo "========================================"
echo ""

FAILED=0

# ============================================================================
# C Code Linting (delegates to check-static-analysis.sh)
# ============================================================================
if ! bash "$SCRIPT_DIR/check-static-analysis.sh"; then
    FAILED=1
fi

# ============================================================================
# Rust Code Linting (delegates to check-clippy.sh)
# ============================================================================
if ! bash "$SCRIPT_DIR/check-clippy.sh"; then
    FAILED=1
fi

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "========================================"
    echo "  ✗ Linting Check FAILED"
    echo "========================================"
    exit 1
else
    echo "========================================"
    echo "  ✓ Linting Check PASSED"
    echo "========================================"
    exit 0
fi
