#!/bin/bash

LLVM_BIN=/root/test/llvm-10/build/bin

source="$1"
tmpdir=$(mktemp -d)

set -e
[ -f llvmassignment3 ]

${LLVM_BIN}/clang -emit-llvm -c -o ${tmpdir}/test.bc ${source} -O0 -g3
./llvmassignment3 ${tmpdir}/test.bc
ret=$?

rm -r ${tmpdir}
exit $ret
