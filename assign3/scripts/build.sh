#!/bin/bash

LLVM_DIR=/root/test/llvm-10/build

set -e
mkdir -p build
cd build
cmake -DLLVM_DIR=${LLVM_DIR} -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
cd - > /dev/null
cp build/llvmassignment3 .
