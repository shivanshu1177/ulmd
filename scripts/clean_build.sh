#!/bin/bash
set -euo pipefail

BUILD_DIRS=(
    "build"
    "test_build" 
    "clang_build"
    "cmake_build"
    "debug_build"
    "release_build"
)

echo "Cleaning build directories..."

for dir in "${BUILD_DIRS[@]}"; do
    if [[ -d "${dir}" ]]; then
        echo "  Removing ${dir}/"
        rm -rf "${dir}"
    fi
done

# Clean temporary files (restrict to safe locations for security)
echo "Cleaning temporary files..."
echo "  Removing object files from build directories..."
if find . -path "./build*" -name "*.o" -type f -delete 2>/dev/null; then
    echo "    ✅ Object files cleaned"
else
    echo "    ℹ️ No object files found or cleanup failed"
fi

echo "  Removing archive files from build directories..."
if find . -path "./build*" -name "*.a" -type f -delete 2>/dev/null; then
    echo "    ✅ Archive files cleaned"
else
    echo "    ℹ️ No archive files found or cleanup failed"
fi

echo "  Removing core dump files from current directory..."
if find . -maxdepth 1 -name "core" -type f -delete 2>/dev/null; then
    echo "    ✅ Core dump files cleaned"
else
    echo "    ℹ️ No core dump files found"
fi

# Clean additional temporary files
echo "  Removing additional temporary files..."
temp_patterns=("*.tmp" "*.bak" "*~" ".DS_Store")
for pattern in "${temp_patterns[@]}"; do
    if find . -name "${pattern}" -type f -delete 2>/dev/null; then
        echo "    ✅ Cleaned ${pattern} files"
    fi
done

echo "Clean complete."