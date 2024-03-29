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
#include "mcjit.h"

#include <stdio.h> /* puts(), fopen() */

#define FALSE 0
#define TRUE 1

int main(int argc, char **argv)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  LLVMContextRef C = LLVMContextCreate();
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext("heLLoVM-C", C);
  char *errors = 0;

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
      LLVMValueRef GV = LLVMAddGlobal(M, greetType, "greetings");
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
  }

  LLVMExecutionEngineRef EE;

  if (CreateMCJIT(&EE, M, &errors)) {
    puts(errors);
    LLVMDisposeMessage(errors);
    LLVMDisposeModule(M);
    LLVMContextDispose(C);
    return 1;
  }

  uint64_t f = LLVMGetFunctionAddress(EE, "boo");
  uint64_t gv = LLVMGetGlobalValueAddress(EE, "greetings");

  printf("boo=%" PRIx64 ", greetings=%" PRIx64 "\n", f, gv);
  assert(gv > f);
  assert(f);

  FILE *file = fopen("c.bin", "wb");
  if (file) {
    fwrite((const void*) f, gv - f + 12, 1, file);
    fclose(file);
  }

  typedef int (*callback)(const char*);
  int (*boo) (const char *, callback, unsigned) =
    ((int (*)(const char *, callback, unsigned)) f);
  int ret = boo("hello", puts, 0) + boo("goodbye", puts, 1);
  LLVMDisposeExecutionEngine(EE);
  LLVMContextDispose(C);
  return ret;
}
