#!/bin/bash

# Master quality check script
# Runs all quality checks in sequence
# Used by both CI pipeline and local development (make quality)
# Exit code 0 = all checks passed, 1 = one or more checks failed

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Running All Quality Checks"
echo "========================================"
echo ""

FAILED=0

# Track which checks failed
FAILED_CHECKS=()

# ============================================================================
# Run All Checks
# ============================================================================

echo "═══════════════════════════════════════════════════════════════"
echo "  [1/3] Format Checks"
echo "═══════════════════════════════════════════════════════════════"
if ! scripts/check-format.sh; then
    FAILED=1
    FAILED_CHECKS+=("Format Check")
fi
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "  [2/3] Linting Checks"
echo "═══════════════════════════════════════════════════════════════"
if ! scripts/check-lint.sh; then
    FAILED=1
    FAILED_CHECKS+=("Linting Check")
fi
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "  [3/3] File Consistency"
echo "═══════════════════════════════════════════════════════════════"
if ! scripts/check-files.sh; then
    FAILED=1
    FAILED_CHECKS+=("File Consistency")
fi
echo ""

# ============================================================================
# Final Summary
# ============================================================================
echo "========================================"
if [ $FAILED -eq 1 ]; then
    echo "  ✗ Quality Checks FAILED"
    echo "========================================"
    echo ""
    echo "Failed checks:"
    for check in "${FAILED_CHECKS[@]}"; do
        echo "  ✗ $check"
    done
    echo ""
    echo "See build/logs/ for detailed reports"
    exit 1
else
    echo "  ✓ All Quality Checks PASSED!"
    echo "========================================"
    exit 0
fi
