#include "llvm-c/Core.h"
#include "llvm-c/LLJIT.h"
#include "llvm-c/OrcEE.h"
#include "llvm-c/Target.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm-c/Transforms/Utils.h"
#include <assert.h>
#ifndef NDEBUG
# include "llvm-c/Analysis.h"
#endif
#include "orc.h"

#include <stdio.h> /* puts(), fopen() */
#include <stdlib.h> /* atoi() */

#define FALSE 0
#define TRUE 1

int main(int argc, char **argv)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  LLVMOrcLLJITRef Jit = NULL;
  LLVMOrcThreadSafeContextRef TSC = LLVMOrcCreateNewThreadSafeContext();
  LLVMErrorRef Err;

  LLVMOrcJITTargetMachineBuilderRef JTMB;
  {
    if ((Err = LLVMOrcJITTargetMachineBuilderDetectHost(&JTMB))) {
    err_exit:
      LLVMOrcDisposeThreadSafeContext(TSC);
      LLVMOrcDisposeLLJIT(Jit);
      char *ErrMsg = LLVMGetErrorMessage(Err);
      fprintf(stderr, "Error: %s\n", ErrMsg);
      LLVMDisposeErrorMessage(ErrMsg);
      return 1;
    }

    LLVMOrcLLJITBuilderRef JB = LLVMOrcCreateLLJITBuilder();
    LLVMOrcLLJITBuilderSetJITTargetMachineBuilder(JB, JTMB);

    if ((Err = LLVMOrcCreateLLJIT(&Jit, JB)))
      goto err_exit;
  }

  int count = argc > 1 ? atoi(argv[1]) : 1;

loop:
  LLVMOrcJITDylibRef JD = LLVMOrcExecutionSessionCreateBareJITDylib
    (LLVMOrcLLJITGetExecutionSession(Jit), "<main>");

  {
    LLVMOrcDefinitionGeneratorRef DG;

    if ((Err = LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess
         (&DG, LLVMOrcLLJITGetGlobalPrefix(Jit), NULL, NULL)))
      goto err_exit;

    LLVMOrcJITDylibAddGenerator(JD, DG);
  }

  {
    LLVMOrcThreadSafeModuleRef TSM;
    LLVMContextRef C = LLVMOrcThreadSafeContextGetContext(TSC);
    LLVMModuleRef M = LLVMModuleCreateWithNameInContext("heLLoVM-C", C);
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
      LLVMValueRef GV = LLVMAddGlobal(M, greetType, "greetings");
      LLVMSetInitializer(GV, LLVMConstArray(greetType, greets, 2));
      LLVMSetGlobalConstant(GV, TRUE);
      LLVMSetUnnamedAddress(GV, LLVMLocalUnnamedAddr);
      LLVMSetLinkage(GV, LLVMExternalLinkage);
      LLVMSetAlignment(GV, 1);
      LLVMSetSection(GV, ".text");

      LLVMBuilderRef builder = LLVMCreateBuilder();
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
    TSM = LLVMOrcCreateNewThreadSafeModule(M, TSC);
    if ((Err = LLVMOrcLLJITAddLLVMIRModule(Jit, JD, TSM)))
      goto err_exit;
  }

  LLVMOrcJITTargetAddress booAddr, greetingsAddr;
  if ((Err = LLVM_Lookup(Jit, JD, &booAddr, "boo")) ||
      (Err = LLVM_Lookup(Jit, JD, &greetingsAddr, "greetings")))
    goto err_exit;

  printf("boo=%" PRIx64 ", greetings=%" PRIx64 "\n", booAddr, greetingsAddr);
#if 0 // TODO: How to determine the length of the code?
  // For some reason, ORCv2 on LLVM later than LLVM 13 may emit
  // "greetings" before "boo" while MCJIT always seems to follow the
  // same order.
  assert(gv > f);
  assert(f);

  FILE *file = fopen("c.bin", "wb");
  if (file) {
    fwrite((const void*) f, gv - f + 12, 1, file);
    fclose(file);
  }
#endif

  typedef int (*callback)(const char*);
  int (*boo) (const char *, callback, unsigned) =
    ((int (*)(const char *, callback, unsigned)) booAddr);
  int ret = boo("hello", puts, 0) + boo("goodbye", puts, 1);
#if LLVM_VERSION_MAJOR >= 14
  LLVM_Remove(LLVMOrcLLJITGetExecutionSession(Jit), JD);
#endif

  if (--count > 0)
    goto loop;
  LLVMOrcDisposeThreadSafeContext(TSC);
  LLVMOrcDisposeLLJIT(Jit);
  return ret;
}
