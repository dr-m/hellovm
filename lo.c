#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm-c/Transforms/Utils.h"
#include "llvm-c/ExecutionEngine.h"
#include <assert.h>
#ifndef NDEBUG
# include "llvm-c/Analysis.h"
# include <string.h> /* memcmp() */
#endif

#include <string.h> /* memcpy() */
#include <stdio.h> /* puts(), fopen() */
#include <stdlib.h> /* posix_memalign() */

#include <unistd.h> /* sysconf(_SC_PAGESIZE) */
#include <sys/mman.h> /* mmap(), mprotect() */

#define FALSE 0
#define TRUE 1

int main(int argc, char **argv)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  LLVMContextRef C = LLVMContextCreate();
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext("hELFoVM-C", C);

  char *TargetTriple = LLVMGetDefaultTargetTriple();
  char *errors = 0;
  LLVMTargetRef Target;
  LLVMGetTargetFromTriple(TargetTriple, &Target, &errors);

  if (!Target) {
  fail:
    puts(errors);
    LLVMDisposeMessage(errors);
    LLVMDisposeMessage(TargetTriple);
    LLVMDisposeModule(M);
    LLVMContextDispose(C);
    return 1;
  }

  LLVMTargetMachineRef TM =
    LLVMCreateTargetMachine(Target, TargetTriple, "generic", "",
                            LLVMCodeGenLevelDefault, LLVMRelocPIC,
                            LLVMCodeModelDefault);
  LLVMSetTarget(M, TargetTriple);
  LLVMDisposeMessage(TargetTriple);

  {
    LLVMTypeRef stringType = LLVMPointerType(LLVMInt8TypeInContext(C), 0);
    LLVMTypeRef intType = LLVMInt32TypeInContext(C);
    LLVMTypeRef PutsType = LLVMFunctionType(intType, &stringType, 1, FALSE);
    LLVMTypeRef PutsArgs[3];
    PutsArgs[0] = stringType;
    PutsArgs[1] = LLVMPointerType(PutsType, 0);
    PutsArgs[2] = stringType;
    LLVMTypeRef FT = LLVMFunctionType(intType, PutsArgs, 3, FALSE);

    LLVMValueRef TheFunction = LLVMAddFunction(M, "boo", FT);
    LLVMSetLinkage(TheFunction, LLVMExternalLinkage);

    {
      LLVMBuilderRef builder = LLVMCreateBuilderInContext(C);
      LLVMBasicBlockRef BB =
        LLVMAppendBasicBlockInContext(C, TheFunction, "entry");
      LLVMPositionBuilderAtEnd(builder, BB);

      LLVMValueRef args[3];
      assert(LLVMCountParams(TheFunction) == 3);
      LLVMGetParams(TheFunction, args);

      LLVMValueRef c1 = LLVMBuildCall2(builder, PutsType, args[1],
                                       args, 1, "");
      LLVMValueRef c2 = LLVMBuildCall2(builder, PutsType, args[1],
                                       &args[2], 1, "");
      LLVMBuildRet(builder, LLVMBuildAdd(builder, c1, c2, ""));
      LLVMDisposeBuilder(builder);
    }

    /* LLVMDumpModule(M); */

    assert(!LLVMVerifyFunction(TheFunction, LLVMPrintMessageAction));

    {
      LLVMPassManagerRef PM = LLVMCreateFunctionPassManagerForModule(M);
      LLVMAddPromoteMemoryToRegisterPass(PM);
      LLVMAddInstructionCombiningPass(PM);
      LLVMAddReassociatePass(PM);
      LLVMAddGVNPass(PM);
      LLVMAddCFGSimplificationPass(PM);
      LLVMInitializeFunctionPassManager(PM);

      LLVMRunFunctionPassManager(PM, TheFunction);
      LLVMDisposePassManager(PM);
    }
  }

  LLVMMemoryBufferRef ObjBuffer;
  if (LLVMTargetMachineEmitToMemoryBuffer(TM, M, LLVMObjectFile, &errors,
                                          &ObjBuffer))
    goto fail;
  LLVMDisposeTargetMachine(TM);
  LLVMDisposeModule(M);
  LLVMContextDispose(C);

  const char *elf= LLVMGetBufferStart(ObjBuffer);
  const size_t elfsize = LLVMGetBufferSize(ObjBuffer);
  FILE *f = fopen("lo-c.o", "wb");

  if (f) {
    fwrite(elf, elfsize, 1, f);
    fclose(f);
  }

  assert(!memcmp(elf, "\177ELF", 4));
  assert(elf[4] == 2); /*64-bit*/
  assert(elf[6] == 1);
  assert(*(const uint16_t*) &elf[0x34] == 64);
  /* number of sections */
  assert(*(const uint16_t*) &elf[0x3c] == 7 ||
         *(const uint16_t*) &elf[0x3c] == 8/* POWER */);
  /* section header size */
  assert(*(const uint16_t*) &elf[0x3a] == 64);
  const size_t *sections = (const size_t*)(elf + *(const size_t*) &elf[0x28]);
  assert(elf + elfsize > (char*)(*sections + 8 * 7));
  assert(((const uint32_t*) &sections[8 * 2])[1] == 1);
  char *text = (char*) &elf[sections[8 * 2 + 3]];
  size_t textsize = sections[8 * 2 + 4];

  printf("size: %zu\n", textsize);

  long sz = sysconf(_SC_PAGESIZE);
  size_t size = (textsize + (sz - 1)) & ~(sz - 1);
  void *buf =
    mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (buf == MAP_FAILED) {
    LLVMDisposeMemoryBuffer(ObjBuffer);
    return 2;
  }

  memcpy(buf, text, textsize);
  LLVMDisposeMemoryBuffer(ObjBuffer);
  mprotect(buf, size, PROT_READ | PROT_EXEC);
  typedef int (*callback)(const char*);
  int (*boo) (const char *, callback, const char *) =
    ((int (*)(const char *, callback, const char *)) buf);
  int ret = boo("hello", puts, "world") + boo("goodbye", puts, "all");
  munmap(buf, size);
  return ret;
}
