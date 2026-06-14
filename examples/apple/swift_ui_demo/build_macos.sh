#!/bin/bash
# Build the SwiftUI demo from the command line (macOS).
#
# Usage:
#   cd examples/apple/swift_ui_demo
#   ./build_macos.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"

INC_DIR="${REPO_ROOT}/inc"
PI_DIR="${REPO_ROOT}/src/pi"
VM_DIR="${BUILD_DIR}/_deps/vector_math-src/inc"
LIB_DIR="${BUILD_DIR}"

echo "=== Campello Audio SwiftUI Demo — Command Line Build ==="
echo "Repo root: ${REPO_ROOT}"
echo "Build dir: ${BUILD_DIR}"

# Verify the library exists
if [ ! -f "${LIB_DIR}/libcampello_audio.dylib" ]; then
    echo "Error: libcampello_audio.dylib not found in ${LIB_DIR}"
    echo "Build the library first: cmake -B build && cmake --build build"
    exit 1
fi

cd "${SCRIPT_DIR}"

# Compile Objective-C++ wrapper
echo "→ Compiling CampelloAudioEngine.mm..."
clang++ -c CampelloAudioEngine.mm -o CampelloAudioEngine.o \
    -I"${INC_DIR}" \
    -I"${PI_DIR}" \
    -I"${VM_DIR}" \
    -std=c++20 \
    -fobjc-arc \
    -framework Foundation

# Compile Swift app
echo "→ Compiling Swift sources..."
swiftc \
    SwiftUIDemoApp.swift \
    ContentView.swift \
    -import-objc-header CampelloAudioEngine.h \
    CampelloAudioEngine.o \
    -I"${INC_DIR}" \
    -I"${PI_DIR}" \
    -I"${VM_DIR}" \
    -L"${LIB_DIR}" \
    -lcampello_audio \
    -lc++ \
    -framework Foundation \
    -framework SwiftUI \
    -o SwiftUIDemo

echo ""
echo "=== Build complete ==="
echo "Run: ./SwiftUIDemo"
