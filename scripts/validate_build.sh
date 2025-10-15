#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
EXPECTED_BINARIES=(
    "ulmdsim"
    "ingress" 
    "parser"
    "worker_risk"
    "ringctl"
    "tsc_cal"
)

echo "Build Validation Report"
echo "======================"
echo "Build directory: ${BUILD_DIR}"
echo

# Check if build directory exists
if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "❌ Build directory ${BUILD_DIR} does not exist"
    exit 1
fi

# Check library with comprehensive validation
lib_path="${BUILD_DIR}/libulmd.a"
if [[ -f "${lib_path}" ]]; then
    echo "✅ Library: libulmd.a"
    
    # Get file size (cross-platform)
    if command -v stat >/dev/null 2>&1; then
        size_bytes=$(stat -f%z "${lib_path}" 2>/dev/null || stat -c%s "${lib_path}" 2>/dev/null || echo 0)
        size_kb=$(( size_bytes / 1024 ))
        echo "   Size: ${size_kb} KB (${size_bytes} bytes)"
        
        # Validate library is not empty
        if [[ ${size_bytes} -eq 0 ]]; then
            echo "   ⚠️  Warning: Library file is empty"
        elif [[ ${size_bytes} -lt 1024 ]]; then
            echo "   ⚠️  Warning: Library file is very small (< 1KB)"
        fi
    else
        echo "   Size: Unable to determine (stat command not available)"
    fi
    
    # Check if it's a valid archive
    if command -v file >/dev/null 2>&1; then
        file_type=$(file "${lib_path}" 2>/dev/null || echo "unknown")
        if [[ "${file_type}" == *"archive"* ]] || [[ "${file_type}" == *"ar archive"* ]]; then
            echo "   ✅ Valid archive format"
        else
            echo "   ❌ Invalid archive format: ${file_type}"
        fi
    fi
    
    # List archive contents if possible
    if command -v ar >/dev/null 2>&1; then
        object_count=$(ar t "${lib_path}" 2>/dev/null | wc -l || echo 0)
        echo "   Object files: ${object_count}"
        if [[ ${object_count} -eq 0 ]]; then
            echo "   ❌ No object files in archive"
        fi
    fi
else
    echo "❌ Library: libulmd.a missing"
    missing_count=$((missing_count + 1))
fi

echo

# Check binaries
missing_count=0
for binary in "${EXPECTED_BINARIES[@]}"; do
    binary_path="${BUILD_DIR}/${binary}"
    if [[ -f "${binary_path}" && -x "${binary_path}" ]]; then
        echo "✅ Binary: ${binary}"
        size_kb=$(( $(stat -f%z "${binary_path}" 2>/dev/null || stat -c%s "${binary_path}" 2>/dev/null || echo 0) / 1024 ))
        echo "   Size: ${size_kb} KB"
    else
        echo "❌ Binary: ${binary} missing or not executable"
        ((missing_count++))
    fi
done

echo
if [[ ${missing_count} -eq 0 ]]; then
    echo "✅ All expected binaries present and executable"
    echo "Build validation: PASSED"
    exit 0
else
    echo "❌ ${missing_count} binaries missing"
    echo "Build validation: FAILED"
    exit 1
fi