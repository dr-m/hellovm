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

#include <stdio.h> /* puts(), fopen() */

#include <sys/mman.h> /* mprotect() */

#define FALSE 0
#define TRUE 1

int main(int argc, char **argv)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  LLVMContextRef C = LLVMContextCreate();
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext("heLLoVM-C", C);

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
    PutsArgs[2] = intType;
    LLVMTypeRef FT = LLVMFunctionType(intType, PutsArgs, 3, FALSE);

    LLVMValueRef TheFunction = LLVMAddFunction(M, "boo", FT);
    LLVMSetLinkage(TheFunction, LLVMExternalLinkage);

    {
      LLVMValueRef greets[2];
      greets[0] = LLVMConstString("world", 6, TRUE);
      greets[1] = LLVMConstString("all\0\0", 6, TRUE);

      LLVMTypeRef greetType = LLVMArrayType(LLVMTypeOf(greets[0]), 2);
      LLVMValueRef GV = LLVMAddGlobal(M, greetType, "msg");
      LLVMSetInitializer(GV, LLVMConstArray(greetType, greets, 2));
      LLVMSetGlobalConstant(GV, TRUE);
      LLVMSetUnnamedAddress(GV, LLVMLocalUnnamedAddr);
      LLVMSetLinkage(GV, LLVMInternalLinkage);
      LLVMSetAlignment(GV, 1);
      LLVMSetSection(GV, ".text");

      LLVMBuilderRef builder = LLVMCreateBuilderInContext(C);
      LLVMBasicBlockRef BB =
        LLVMAppendBasicBlockInContext(C, TheFunction, "entry");
      LLVMPositionBuilderAtEnd(builder, BB);

      LLVMValueRef args[3];
      assert(LLVMCountParams(TheFunction) == 3);
      LLVMGetParams(TheFunction, args);

      LLVMValueRef c1 = LLVMBuildCall2(builder, PutsType, args[1],
                                       args, 1, "");
      LLVMValueRef indices[2];
      indices[0] = LLVMConstInt(intType, 0, FALSE);
      indices[1] = args[2];

      LLVMValueRef c2a =
        LLVMBuildInBoundsGEP2(builder, LLVMGlobalGetValueType(GV), GV,
                              indices, 2, "m");
      c2a = LLVMBuildBitCast(builder, c2a, stringType, "");
      LLVMValueRef c2 = LLVMBuildCall2(builder, PutsType, args[1],
                                       &c2a, 1, "");
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
  FILE *f = fopen("llo-c.o", "wb");

  if (f) {
    fwrite(elf, elfsize, 1, f);
    fclose(f);
  }

  assert(!memcmp(elf, "ELF", 4));
  assert(elf[4] == 2); /*64-bit*/
  assert(elf[6] == 1);
  assert(*(const uint16_t*) &elf[0x34] == 64);
#if LLVM_VERSION_MAJOR < 14
  /* number of sections */
  assert(*(const uint16_t*) &elf[0x3c] == 7);
#else
  /* LLVM 14 and 15 create two .text sections and a .text.rela section
  for the reference to the global variable */
  const uint16_t numSections = *(const uint16_t*) &elf[0x3c];
  assert(numSections == 7 || numSections == 9);
#endif
  /* section header size */
  assert(*(const uint16_t*) &elf[0x3a] == 64);
  const size_t *sections = (const size_t*)(elf + *(const size_t*) &elf[0x28]);
  assert(elf + elfsize > (char*)(*sections + 8 * 7));
  assert(((const uint32_t*) &sections[8 * 2])[1] == 1);
  char *text = (char*) &elf[sections[8 * 2 + 3]];
  size_t textsize = sections[8 * 2 + 4];
#if LLVM_VERSION_MAJOR >= 14
  if (numSections == 9) {
    assert(&elf[sections[8 * 4 + 3]] == text + textsize);
    textsize += sections[8 * 4 + 4];
    /* TODO: apply .text.rela */
  }
#endif

  printf("size: %zu\n", textsize);

  mprotect((void*)((size_t)text & ~((size_t)4095)), (textsize + 4095) & ~4095U,
           PROT_READ | PROT_WRITE | PROT_EXEC);

  typedef int (*callback)(const char*);
  int (*boo) (const char *, callback, unsigned) =
    ((int (*)(const char *, callback, unsigned)) text);
  int ret = boo("hello", puts, 0) + boo("goodbye", puts, 1);
  LLVMDisposeMemoryBuffer(ObjBuffer);
  return ret;
}
