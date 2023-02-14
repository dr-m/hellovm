#include "llvm/ExecutionEngine/ExecutionEngine.h"

extern "C"
LLVMBool CreateMCJIT(LLVMExecutionEngineRef *OutJIT,
                     LLVMModuleRef M, char **OutError)
{
  std::string Error;
  if (llvm::ExecutionEngine *EE =
      llvm::EngineBuilder(std::unique_ptr<llvm::Module>(llvm::unwrap(M))).
      setEngineKind(llvm::EngineKind::JIT).
      setErrorStr(&Error).
      setOptLevel(llvm::CodeGenOpt::Default).
      setRelocationModel(llvm::Reloc::PIC_).
      create()) {
    EE->finalizeObject();
    *OutJIT = llvm::wrap(EE);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}
