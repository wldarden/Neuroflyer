#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

cmake -B build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1
cmake --build build --target neuroflyer -j "$(sysctl -n hw.ncpu)" 2>&1 | tail -3

echo ""
exec ./build/neuroflyer "$@"
