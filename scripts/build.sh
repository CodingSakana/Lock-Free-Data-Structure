#!/bin/bash

set -euo pipefail

rm -rf build-release build-debug

echo "start building project..."

mkdir -p build-release
pushd build-release > /dev/null
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja -j$(nproc)
popd > /dev/null

mkdir -p build-debug
pushd build-debug > /dev/null
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
ninja -j$(nproc)
popd > /dev/null

echo -e "\nproject built successfully!"
