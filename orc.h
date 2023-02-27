LLVMErrorRef LLVM_Lookup
(LLVMOrcLLJITRef J, LLVMOrcJITDylibRef JD, LLVMOrcJITTargetAddress *addr,
 const char *name);
#if LLVM_VERSION_MAJOR >= 14
LLVMErrorRef LLVM_Remove(LLVMOrcExecutionSessionRef ES, LLVMOrcJITDylibRef JD);
#endif
