#!/bin/bash
set -e

if [ "$(uname)" != "Darwin" ]; then
    echo "run_macos_example.sh only supports macOS." >&2
    exit 1
fi

cmake -B build -DBUILD_EXAMPLES=ON "$@"
cmake --build build

APP_PATH="build/examples/apple/macos_keyboard_sampler/campello_audio_macos_sampler.app"

if [ ! -d "$APP_PATH" ]; then
    echo "Example app not found at $APP_PATH" >&2
    exit 1
fi

open "$APP_PATH"
