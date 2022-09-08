#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#ifndef NDEBUG
# include "llvm/IR/Verifier.h"
#endif
#if LLVM_VERSION_MAJOR >= 14
# include "llvm/MC/TargetRegistry.h"
#else
# include "llvm/Support/TargetRegistry.h"
#endif
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include <sys/mman.h> /* mprotect() */

#if defined __GNUC__ && !defined __clang__ && __GNUC__ == 4
namespace std { using llvm::make_unique; }
#endif

#if LLVM_VERSION_MAJOR < 10
namespace llvm { using Align = int; }
#endif

class NullResolver : public llvm::LegacyJITSymbolResolver
{
public:
  llvm::JITSymbol findSymbol(const std::string &) override { return nullptr; }

  llvm::JITSymbol findSymbolInLogicalDylib(const std::string &) override {
    return nullptr;
  }
};

int main(int argc, char **argv)
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  auto C = std::make_unique<llvm::LLVMContext>();
  auto M = std::make_unique<llvm::Module>("heLLoVM", *C);

  std::string Error;
  const auto TargetTriple = llvm::sys::getDefaultTargetTriple();
  const auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!Target) {
    llvm::errs() << Error;
    return 1;
  }

  llvm::TargetOptions opt;
  std::unique_ptr<llvm::TargetMachine>
    TM(Target->createTargetMachine(TargetTriple, "generic", "", opt,
                                   llvm::Optional<llvm::Reloc::Model>
                                   (llvm::Reloc::PIC_)));
  M->setDataLayout(TM->createDataLayout());
  M->setTargetTriple(TargetTriple);

  llvm::SmallVector<char, 0> ObjBufferSV;

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
                                         llvm::GlobalValue::InternalLinkage,
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

  auto EE = llvm::EngineBuilder(std::move(M)).
    setEngineKind(llvm::EngineKind::JIT).
    setSymbolResolver(std::make_unique<NullResolver>()).
    setErrorStr(&Error).
    setOptLevel(llvm::CodeGenOpt::Default).
    setRelocationModel(llvm::Reloc::PIC_).
    setCodeModel(llvm::CodeModel::Tiny).
    setMCPU("native").
    setVerifyModules(true).
    create(TM.release());

  EE->finalizeObject();

#if LLVM_VERSION_MAJOR >= 11
  if (EE->hasError()) {
    llvm::errs() << EE->getErrorMessage();
    return 2;
  }
#endif

  uint64_t f = EE->getFunctionAddress("boo");
  uint64_t gv = EE->getGlobalValueAddress("greetings");

  printf("boo=%llx, greetings=%llx\n", f, gv);
  assert(gv > f);
  assert(f);

  if (FILE *file = fopen("cc.bin", "wb")) {
    fwrite((const void*) f, gv - f + 12, 1, file);
    fclose(file);
  }

  typedef int (*callback)(const char*);
  auto boo =
    reinterpret_cast<int(*)(const char *, callback, unsigned)>(f);
  int ret = boo("hello ", puts, 0) + boo("goodbye ", puts, 1);
  delete EE;
  return ret;
}
