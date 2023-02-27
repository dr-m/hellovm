#include "llvm-c/LLJIT.h"
#include "llvm-c/Orc.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/CBindingWrapping.h"

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::LLJIT, LLVMOrcLLJITRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::JITDylib, LLVMOrcJITDylibRef)

extern "C"
LLVMErrorRef LLVM_Lookup
(LLVMOrcLLJITRef J, LLVMOrcJITDylibRef JD, LLVMOrcJITTargetAddress *addr,
 const char *name)
{
  auto Sym = unwrap(J)->lookup(*unwrap(JD), name);
  if (!Sym) {
    *addr = 0;
    return wrap(Sym.takeError());
  }
#if LLVM_VERSION_MAJOR < 15
  *addr = Sym->getAddress();
#else
  *addr = Sym->getValue();
#endif
  return LLVMErrorSuccess;
}

#if LLVM_VERSION_MAJOR >= 14
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::ExecutionSession,
                                   LLVMOrcExecutionSessionRef)

extern "C"
LLVMErrorRef LLVM_Remove(LLVMOrcExecutionSessionRef ES, LLVMOrcJITDylibRef JD)
{
  if (llvm::Error Err{unwrap(ES)->removeJITDylib(*unwrap(JD))})
    return wrap(std::move(Err));
  return LLVMErrorSuccess;
}
#endif
