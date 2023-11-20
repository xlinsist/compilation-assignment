# Assignment 2

## Build

Add to `CMakeLists.txt` :

```cmake
set(CMAKE_CXX_STANDARD 17)
add_compile_options("-fno-rtti")
```

Script to build is stored at `scripts/build.sh`, you may change the value of variable `LLVM_DIR` (and `LLVM_PROJECT` if needed).

Command to build:

```
make
```

## Run

Script to run a single testcase is stored at `scripts/run.sh`, you may change the value of variable `LLVM_BIN` (and `LLVM_PROJECT` if needed).

Command to run this assignment if the name of testfile (C source) is `testfile.c`:

```
make run
```

Otherwise change `<testfile>` into the real name of your testfile (C source) with command below:

```
make run testfile=<testfile>
```

## Test

The testcases is stored in `assign2-tests`.

First build the checker (written in rust):

```
cd checker
cargo build -r
cp target/release/assign2-checker ..
cd ..
```

Then run command:

```
make test
```

To enable strict format check:

```
STRICT=1 make test
```

## 调试样例

clang的前端生成IR-O0：
```
/root/test/llvm-10/build/bin/clang -Xclang -disable-O0-optnone -O0 -emit-llvm -S tests/test00.c -o test00.ll
```

clang的前端生成IR-O3：
```
/root/test/llvm-10/build/bin/opt -S -mem2reg -o test00-reg.ll test00.ll
```