# heLLoVM: a simple "hello world" using LLVM IR

These programs demonstrate how to let generate and invoke stand-alone
position-independent code using LLVM IR, both the C and C++ interface.

This has been tested to work with LLVM 9 and LLVM 13.
With LLVM 14 and 15, there is an issue:
https://github.com/llvm/llvm-project/issues/57274

The CMake tooling is optional. You may also use the following:

```sh
cc -o hellovmc llo.c $(llvm-config --cflags --ldflags --system-libs --libs core)
c++ -o hellovm llo.cc $(llvm-config --cxxflags --ldflags --system-libs --libs core)
```
