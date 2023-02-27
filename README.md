# heLLoVM: a simple "hello world" using LLVM IR

These programs demonstrate how to let generate and invoke stand-alone
position-independent code using LLVM IR, both C and C++ interfaces.

The MCJIT interface has been tested on various platforms with
LLVM 9, 11, 13, 14, 15.

The ORCv2 interface has been tested with LLVM 13, 14, 15.
When built with LLVM 13, the C++ version will leak memory, because
`llvm::orc::ExecutionSession::removeJITDylib()` is not available there.

The CMake tooling is optional and possibly incomplete.
You may also invoke the following directly:

```sh
c++ -o hellovm llo.cc $(llvm-config --cxxflags --ldflags --system-libs --libs core)
cc -c llo.c $(llvm-config --cflags)
c++ -c mcjit.cc $(llvm-config --cxxflags)
c++ -o hellovmc llo.o mcjit.o $(llvm-config --ldflags --system-libs --libs core)
# For LLVM-13 or later:
c++ -o hellorc llo-orc.cc $(llvm-config --cxxflags --ldflags --system-libs --libs core)
cc -c llo-orc.c $(llvm-config --cflags)
c++ -c orc.cc $(llvm-config --cxxflags)
c++ -o hellorcc llo-orc.o orc.o $(llvm-config --ldflags --system-libs --libs core)
```
Note: You may have to replace `llvm-config` with something that
includes a version number suffix, such as `llvm-config-13`,
and `cc` and `c++` with the names of your preferred C and C++ compilers.

When run, the programs will write the generated machine code into a file
`cc.bin` (for the C++ `hellovm`) or `c.bin` (for the C `hellovmc`) and then
attempt to execute the code. Something like this should be written to
the standard output. The following is for `hellovm` on AMD64:
```
boo=7f89f01db000, greetings=7f89f01db02a
hello 
world
goodbye 
all
```
On LLVM 14 and 15 due to
https://github.com/llvm/llvm-project/issues/57274 the `greetings` will
not be stored right after the end of the function `boo`, but in a
separate page.

## Library interface notes

`LLVMCreateMCJITCompilerForModule()` in the `llvm-c` library does not
provide access to all options of the `llvm::EngineBuilder()`. Most notably,
there does not seem to be a way to specify a relocation model, such as
position-independent code. Therefore, we use our own wrapper
`CreateMCJIT()`.

An earlier version of this experiment generated an `.o` file in a
memory buffer and then parsed that buffer as ELF.

An attempt to use `llvm::RuntimeDyld::loadObject()` was not successful;
`llvm::RuntimeDyld::LoadedObjectInfo::getSectionLoadAddress()` would
return something to which relocations were not applied.

Our ORCv2 interface depends on
`llvm::orc::SelfExecutorProcessControl::Create()`, which was
introduced in LLVM 13. The programs `hellorc` and `hellorcc` take an
optional parameter, to specify the number of loop iterations. You can
execute
```sh
time ./hellorc 10000000 > /dev/null &
time ./hellorcc 10000000 > /dev/null &
top
```
and monitor the memory usage and consumed CPU time. When using LLVM 14
or LLVM 15, the memory usage should remain constant. When using LLVM 13,
the memory usage will keep growing; see below.

Our ORCv2 interface would invoke
`llvm::orc::ExecutionSession::removeJITDylib()` to remove generated
dynamic libraries, but that function does not exist in LLVM 13.
Therefore, `hellorc` will report a memory leak when built with
`-fsanitize=address`. The C program `hellorcc` will avoid the leak,
apparently thanks to `LLVMOrcDisposeLLJIT()`.

## Platform notes

### AMD64
On LLVM 9 to 13, the program is readily linked position-independent code
straight from the compiler.

On LLVM 14 and 15, there is an issue that a `.text.rela` section for
the constant will not be replaced, and two `.text` sections will be generated:
https://github.com/llvm/llvm-project/issues/57274

### ARMv8
On LLVM 9 on SLES 15 SP2 as well as on LLVM 11 on Debian 11,
a `.rela.text` section will be created with two relocations for the
reference to the constant array:
```
Relocation section '.rela.text' at offset 0x178 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000014  000600000113 R_AARCH64_ADR_PRE 0000000000000000 .text + 3c
000000000018  000600000115 R_AARCH64_ADD_ABS 0000000000000000 .text + 3c
```
This should be applied to the following code:
```
  14:	90000008 	adrp	x8, 0 <boo>
  18:	91000108 	add	x8, x8, #0x0
```

### IBM zSeries (s390x)
On LLVM 14, two `.text` sections will be generated, like on AMD64.

On LLVM 13, a `.rela.text` section will be created with one relocation for the
reference to the constant array:
```
Relocation section '.rela.text' at offset 0x138 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
00000000001c  00030000001a R_390_GOTENT      0000000000000038 __unnamed_1 + 2
```
This should be applied to the following code:
```
  1a:   c0 10 00 00 00 00   larl    %r1,1a <boo+0x1a>
```
Thanks to Daniel Black for testing this.
