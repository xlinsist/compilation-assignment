
## assign1
```
$ cd /root/test/assign1/ast-interpreter/build
$ ./ast-interpreter "`cat /root/test/assign1/tests/test00.c`"
$ /root/test/llvm-10/build/bin/clang -Xclang -ast-dump -fsyntax-only /root/test/assign1/tests/test00.c
```