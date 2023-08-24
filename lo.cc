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
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <cstring> /* memcpy() */
#include <unistd.h> /* sysconf(_SC_PAGESIZE) */
#include <sys/mman.h> /* mmap(), mprotect() */

#if defined __GNUC__ && !defined __clang__ && __GNUC__ == 4
namespace std { using llvm::make_unique; }
#endif

#if LLVM_VERSION_MAJOR < 10
namespace llvm { using Align = int; }
#endif

int main(int argc, char **argv)
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  auto C = std::make_unique<llvm::LLVMContext>();
  auto M = std::make_unique<llvm::Module>("hELFoVM", *C);

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
                              {stringType, PutsType->getPointerTo(),
                               stringType},
                              false);
    llvm::Function *TheFunction =
      llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                             "boo", M.get());
    TheFunction->setDoesNotThrow();
    {
      llvm::IRBuilder<> builder(llvm::BasicBlock::Create(*C, "entry",
                                                         TheFunction));
      auto Str = TheFunction->arg_begin();
      auto F = Str;
      llvm::FunctionCallee FC{PutsType, ++F};
      auto c1 = builder.CreateCall(FC, Str);
      auto c2 = builder.CreateCall(FC, ++F);
      builder.CreateRet(builder.CreateAdd(c1, c2));
    }
    // M->dump();
    assert(!llvm::verifyFunction(*TheFunction, &llvm::errs()));

    {
      // Create the analysis managers.
      llvm::LoopAnalysisManager LAM;
      llvm::FunctionAnalysisManager FAM;
      llvm::CGSCCAnalysisManager CGAM;
      llvm::ModuleAnalysisManager MAM;

      // Create the new pass manager builder.
      // Take a look at the PassBuilder constructor parameters for more
      // customization, e.g. specifying a TargetMachine or various debugging
      // options.
      llvm::PassBuilder PB;

      // Register all the basic analyses with the managers.
      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      using OptimizationLevel = llvm::
#if LLVM_VERSION_MAJOR < 14
        PassBuilder::
#endif
        OptimizationLevel;

      PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2).run(*M, MAM);
    }

    {
      llvm::raw_svector_ostream ObjStream(ObjBufferSV);

      llvm::legacy::PassManager PM;
      llvm::MCContext *Ctx;
      if (TM->addPassesToEmitMC(PM, Ctx, ObjStream))
        return 2;
      PM.run(*M);
    }
  }

  const char *elf= ObjBufferSV.begin();
  const size_t elfsize = ObjBufferSV.size();

  if (FILE *f = fopen("lo.o", "wb")) {
    fwrite(elf, elfsize, 1, f);
    fclose(f);
  }

  assert(!memcmp(elf, "\177ELF", 4));
  assert(elf[4] == 2); /*64-bit*/
  assert(elf[6] == 1);
  assert(*reinterpret_cast<const uint16_t*>(elf + 0x34) == 64);
  /* number of sections */
  assert(*reinterpret_cast<const uint16_t*>(elf + 0x3c) == 5 ||
         *reinterpret_cast<const uint16_t*>(elf + 0x3c) == 6/* POWER */);
  /* section header size */
  assert(*reinterpret_cast<const uint16_t*>(elf + 0x3a) == 64);
  const size_t *sections = reinterpret_cast<const size_t*>
    (elf + *reinterpret_cast<const size_t*>(elf + 0x28));
  assert(elf + elfsize > reinterpret_cast<const char*>(*sections + 8 * 7));
  assert(reinterpret_cast<const uint32_t*>(&sections[8 * 2])[1] == 1);
  char *text = const_cast<char*>(&elf[sections[8 * 2 + 3]]);
  size_t textsize = sections[8 * 2 + 4];

  printf("size: %zu\n", textsize);

  long sz = sysconf(_SC_PAGESIZE);
  size_t size = (textsize + (sz - 1)) & ~(sz - 1);
  void *buf = mmap(nullptr, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (buf == MAP_FAILED)
    return 2;
  memcpy(buf, text, textsize);
  mprotect(buf, size, PROT_READ | PROT_EXEC);
  typedef int (*callback)(const char*);
  auto boo =
    reinterpret_cast<int(*)(const char *, callback, const char *)>(buf);
  int ret = boo("hello", puts, "world") + boo("goodbye", puts, "all");
  munmap(buf, size);
  return ret;
}
