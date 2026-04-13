#!/bin/bash
set -e
cmake -B build -DBUILD_TESTS=ON "$@"
cmake --build build
ctest --test-dir build --output-on-failure
