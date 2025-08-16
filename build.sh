#!/bin/bash
set -euo pipefail

# Build configuration
BUILD_DIR="${BUILD_DIR:-build}"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -I include}"

# Platform detection
if [[ "$(uname)" == "Darwin" ]]; then
    PLATFORM_FLAGS="-DULMD_MACOS=1"
elif [[ "$(uname)" == "Linux" ]]; then
    PLATFORM_FLAGS="-DULMD_LINUX=1 -pthread"
else
    echo "Error: Unsupported platform $(uname)" >&2
    exit 1
fi

echo "Building ULMD project..."
echo "Build directory: ${BUILD_DIR}"
echo "Compiler: ${CXX}"
echo "Platform: $(uname)"

# Create build directory
if ! mkdir -p "${BUILD_DIR}"; then
    echo "Error: Failed to create build directory ${BUILD_DIR}" >&2
    exit 1
fi

# Source files
SOURCE_FILES=(
    "src/io.cpp"
    "src/ring.cpp"
    "src/message_pool.cpp"
    "src/telemetry.cpp"
    "src/worker_risk_shard.cpp"
    "src/config.cpp"
    "src/health.cpp"
    "src/metrics.cpp"
    "src/shutdown.cpp"
)

# Compile source files
echo "Compiling source files..."
for src in "${SOURCE_FILES[@]}"; do
    if [[ -f "${src}" ]]; then
        obj="${BUILD_DIR}/$(basename "${src}" .cpp).o"
        echo "  ${src} -> ${obj}"
        if ! "${CXX}" ${CXXFLAGS} ${PLATFORM_FLAGS} -c "${src}" -o "${obj}"; then
            echo "Error: Failed to compile ${src}" >&2
            exit 1
        fi
    else
        echo "Warning: Source file ${src} not found" >&2
    fi
done

# Create library
echo "Creating library..."
if ! ls "${BUILD_DIR}"/*.o >/dev/null 2>&1; then
    echo "Error: No object files found to create library" >&2
    exit 1
fi
if ! ar rcs "${BUILD_DIR}/libulmd.a" "${BUILD_DIR}"/*.o; then
    echo "Error: Failed to create library" >&2
    exit 1
fi

# Application files
APP_FILES=(
    "ulmdsim"
    "ingress"
    "parser"
    "worker_risk"
    "ringctl"
    "tsc_cal"
)

# Build applications
echo "Building applications..."
LDFLAGS="-L${BUILD_DIR} -lulmd"
for app in "${APP_FILES[@]}"; do
    src="apps/${app}.cpp"
    bin="${BUILD_DIR}/${app}"
    if [[ -f "${src}" ]]; then
        echo "  ${src} -> ${bin}"
        if ! "${CXX}" ${CXXFLAGS} ${PLATFORM_FLAGS} "${src}" ${LDFLAGS} -o "${bin}"; then
            echo "Error: Failed to build ${app}" >&2
            exit 1
        fi
    else
        echo "Warning: Application source ${src} not found" >&2
    fi
done

echo "Build complete. Binaries in ${BUILD_DIR}/"