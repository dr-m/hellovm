# heLLoVM: a simple "hello world" using LLVM IR

These programs demonstrate how to let generate and invoke stand-alone
position-independent code using LLVM IR, both the C and C++ interface.

This has been mainly tested with LLVM 9 and LLVM 13.

The CMake tooling is optional and incomplete.
You may also use the following directly:

```sh
cc -o hellovmc llo.c $(llvm-config --cflags --ldflags --system-libs --libs core)
c++ -o hellovm llo.cc $(llvm-config --cxxflags --ldflags --system-libs --libs core)
```

## Platform notes

### AMD64
The program is readily linked position-independent code.
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
