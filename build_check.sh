#!/bin/bash
# build_check.sh — Quick build verification for PTVA
# Run this on Linux: bash build_check.sh

set -e

echo "=== Process Tree Viewer & Analyzer — Build Check ==="
echo ""

# Check for required tools
for tool in gcc make; do
    if ! command -v "$tool" &> /dev/null; then
        echo "ERROR: $tool not found. Install it first."
        exit 1
    fi
done

# Check for ncurses
if ! pkg-config --exists ncurses 2>/dev/null; then
    echo "WARNING: ncurses pkg-config not found. Trying to compile anyway..."
fi

echo "Building ptva..."
make clean
make

echo ""
echo "=== Build successful! ==="
echo ""
echo "Run: ./ptva"
echo "Or:  ./ptva -i 500    (500ms refresh)"
echo ""
echo "Building test programs..."
make tests

echo ""
echo "Test programs:"
echo "  tests/test_fork_zombie  — run in background to demo zombie processes"
echo "  tests/test_reparent     — run in background to demo re-parenting"
echo ""
echo "Demo commands:"
echo "  ./ptva &"
echo "  ./tests/test_fork_zombie &"
echo "  # Watch the zombie appear in ptva, then get collected"
