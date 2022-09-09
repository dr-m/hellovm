#include "llvm/ExecutionEngine/ExecutionEngine.h"

namespace llvm {
  static TargetMachine *unwrap(LLVMTargetMachineRef TM)
  { return reinterpret_cast<TargetMachine *>(TM); }
}

extern "C"
LLVMBool CreateMCJIT(LLVMExecutionEngineRef *OutJIT, LLVMTargetMachineRef TM,
                     LLVMModuleRef M, char **OutError)
{
  std::string Error;
  if (llvm::ExecutionEngine *EE =
      llvm::EngineBuilder(std::unique_ptr<llvm::Module>(llvm::unwrap(M))).
      setEngineKind(llvm::EngineKind::JIT).
      setErrorStr(&Error).
      setOptLevel(llvm::CodeGenOpt::Default).
      setRelocationModel(llvm::Reloc::PIC_).
      setCodeModel(llvm::CodeModel::Tiny).
      setMCPU("native").
      create(llvm::unwrap(TM))) {
    EE->finalizeObject();
    *OutJIT = llvm::wrap(EE);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}
