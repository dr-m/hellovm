#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#ifndef NDEBUG
# include "llvm/IR/Verifier.h"
#endif
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

#if defined __GNUC__ && !defined __clang__ && __GNUC__ == 4
namespace std { using llvm::make_unique; }
#endif

int main(int argc, char **argv)
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  auto EPC = llvm::orc::SelfExecutorProcessControl::Create();
  if (!EPC) {
    llvm::errs() << EPC.takeError();
    return 1;
  }

  std::unique_ptr<llvm::orc::ExecutionSession> ES
    {std::make_unique<llvm::orc::ExecutionSession>(std::move(*EPC))};
  llvm::orc::JITTargetMachineBuilder JTMB
    {ES->getExecutorProcessControl().getTargetTriple()};
  llvm::Expected<llvm::DataLayout> DL{JTMB.getDefaultDataLayoutForTarget()};
  if (!DL) {
    llvm::errs() << DL.takeError();
    return 1;
  }
  llvm::orc::RTDyldObjectLinkingLayer ObjectLayer
    {*ES, []() { return std::make_unique<llvm::SectionMemoryManager>(); }};
  llvm::orc::IRCompileLayer CompileLayer
    {*ES, ObjectLayer,
     std::make_unique<llvm::orc::ConcurrentIRCompiler>(std::move(JTMB))};

  llvm::orc::JITDylib &MainJD = ES->createBareJITDylib("<main>");
  MainJD.addGenerator
    (cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess
              (DL->getGlobalPrefix())));

  auto C = std::make_unique<llvm::LLVMContext>();
  auto M = std::make_unique<llvm::Module>("heLLoVM", *C);
  M->setDataLayout(*DL);
  std::unique_ptr<llvm::orc::MangleAndInterner> Mangle
    {std::make_unique<llvm::orc::MangleAndInterner>(*ES, std::move(*DL))};

  {
    const auto stringType = llvm::Type::getInt8PtrTy(*C);
    const auto intType = llvm::Type::getInt32Ty(*C);
    std::vector<llvm::Type *> PutsArgs{stringType};
    llvm::FunctionType *PutsType =
      llvm::FunctionType::get(intType, PutsArgs, false);
    llvm::FunctionType *FT =
      llvm::FunctionType::get(intType,
                              {stringType, PutsType->getPointerTo(), intType},
                              false);
    llvm::Function *TheFunction =
      llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                             "boo", M.get());
    {
      const auto world =
        llvm::ConstantDataArray::getString(*C, llvm::StringRef{"world", 6},
                                           false);
      const auto all =
        llvm::ConstantDataArray::getString(*C, llvm::StringRef{"all\0\0", 6},
                                           false);
      const auto greetings =
        llvm::ConstantArray::get(llvm::ArrayType::get(world->getType(), 2),
                                 {world, all});
      auto GV = new llvm::GlobalVariable(*M, greetings->getType(), true,
                                         llvm::GlobalValue::ExternalLinkage,
                                         greetings, "greetings");
      GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Local);
      GV->setAlignment(llvm::Align(1));
      GV->setSection(llvm::StringRef{".text", 5});

      llvm::IRBuilder<> builder(llvm::BasicBlock::Create(*C, "entry",
                                                         TheFunction));
      auto Str = TheFunction->arg_begin();
      auto F = Str;
      llvm::FunctionCallee FC{PutsType, ++F};
      auto c1 = builder.CreateCall(FC, Str);
      auto c2a = builder.CreateInBoundsGEP(GV->getValueType(), GV,
                                           {builder.getInt32(0), ++F});
      auto c2 = builder.CreateCall(FC, builder.CreateBitCast(c2a, stringType));

      builder.CreateRet(builder.CreateAdd(c1, c2));
    }
    // M->dump();
    assert(!llvm::verifyFunction(*TheFunction, &llvm::errs()));
  }

  if (llvm::Error Err
      {CompileLayer.add
       (MainJD.getDefaultResourceTracker(),
        llvm::orc::ThreadSafeModule(std::move(M), std::move(C)))}) {
    llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "");
    return 1;
  }

  llvm::Expected<llvm::JITEvaluatedSymbol> booSym
    {ES->lookup({&MainJD}, (*Mangle)("boo"))};
  if (!booSym) {
    llvm::errs() << booSym.takeError() << '\n';
    return 1;
  }
  llvm::Expected<llvm::JITEvaluatedSymbol> greetingsSym
    {ES->lookup({&MainJD}, (*Mangle)("greetings"))};
  if (!greetingsSym) {
    llvm::errs() << greetingsSym.takeError() << '\n';
    return 1;
  }

  uint64_t f = booSym->getAddress();
  uint64_t gv = greetingsSym->getAddress();

  printf("boo=%" PRIx64 ", greetings=%" PRIx64 "\n", f, gv);
#if 0 // TODO: How to determine the length of the code?
  // For some reason, ORCv2 on LLVM later than LLVM 13 may emit
  // "greetings" before "boo" while MCJIT always seems to follow the
  // same order.
  assert(f < gv);
  assert(f);
  if (FILE *file = fopen("orcc.bin", "wb")) {
    fwrite(reinterpret_cast<const void*>(f), gv - f + 12, 1, file);
    fclose(file);
  }
#endif
  typedef int (*callback)(const char*);
  auto boo =
    reinterpret_cast<int(*)(const char *, callback, unsigned)>(f);
  int ret = boo("hello ", puts, 0) + boo("goodbye ", puts, 1);
#if LLVM_VERSION_MAJOR >= 14
  if (llvm::Error Err{ES->removeJITDylib(MainJD)}) {
    llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "");
    return 1;
  }
#endif
  return ret;
}
