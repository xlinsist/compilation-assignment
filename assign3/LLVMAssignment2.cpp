//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include "Dataflow2.h"

using namespace llvm;

enum { PtrTyEqual = -1, PtrTyReturn = -2, PtrTyCall = -3 };

// (pointer, offset)
struct PointerTy : std::pair<Value *, int64_t> {
  PointerTy(Value *val, int64_t off) : pair(val, off) {}
  PointerTy(Value *val) : pair(val, PtrTyEqual) {}
};

// key -> {pointees}
// offset < 0 is used for special purposes
typedef std::map<PointerTy, std::set<PointerTy>> PointerMap;

inline raw_ostream &operator<<(raw_ostream &out, const PointerMap &info) {
  for (auto it : info) {
    Value *ptr = it.first.first;
    int64_t offset = it.first.second;
    if (ptr) {
      out << ptr->getName();
    } else {
      out << "???";
    }
    switch (offset) {
    case PtrTyEqual:
      out << " = ";
      break;
    case PtrTyReturn:
      out << " returns: ";
      break;
    case PtrTyCall:
      out << " => ";
      break;
    default:
      out << "[" << offset << "]: ";
      break;
    }
    bool first = true;
    for (auto p : it.second) {
      if (first) {
        first = false;
      } else {
        out << ", ";
      }
      if (p.first) {
        out << p.first->getName();
      } else {
        out << "???";
      }
      if (p.second > 0) {
        out << "[" << p.second << "]";
      }
    }
    out << '\n';
  }
  return out;
}

std::set<PointerTy> getValues(const PointerMap &map, const PointerTy src) {
  if (map.find(src) == map.end()) {
    return {PointerTy(src.first, 0)};
  } else {
    return map.at(src);
  }
}

void copyValue(PointerMap &dstmap, const PointerTy dst,
               const PointerMap &srcmap, const PointerTy src) {
  auto set = getValues(srcmap, src);
  dstmap[dst].insert(set.begin(), set.end());
}

void copyValue(PointerMap &map, const PointerTy dst, const PointerTy src) {
  copyValue(map, dst, map, src);
}

void rewriteValue(PointerMap &dstmap, const PointerTy dst,
                  const PointerMap &srcmap, const PointerTy src) {
  dstmap[dst] = getValues(srcmap, src);
}

void rewriteValue(PointerMap &map, const PointerTy dst, const PointerTy src) {
  rewriteValue(map, dst, map, src);
}

class PointerVisitor : public DataflowVisitor<PointerMap> {
public:
  PointerVisitor() {}
  void merge(PointerMap *dest, const PointerMap &src) override {
    for (auto it : src) {
      copyValue(*dest, it.first, src, it.first);
    }
  }

  void compDFVal(Instruction *inst, PointerMap *dfval) override {
    if (isa<DbgInfoIntrinsic>(inst))
      return;

    // errs() << "Current instruction: "  << *inst << '\n';

    PointerMap &map = *dfval;
    if (PHINode *phi = dyn_cast<PHINode>(inst)) {
      if (phi->getType()->isPointerTy()) {
        // errs() << "[DEBUG] process PHINode" << '\n';
        for (int i = 0; i < phi->getNumIncomingValues(); i++) {
          copyValue(map, phi, phi->getIncomingValue(i));
        }
      }
    } else if (LoadInst *load = dyn_cast<LoadInst>(inst)) {
      if (load->getType()->isPointerTy()) {
        // errs() << "[DEBUG] process LoadInst" << '\n';
        Value *ptr = load->getPointerOperand();
        for (PointerTy v : getValues(map, ptr)) {
          copyValue(map, load, v);
        }
      }
    } else if (StoreInst *store = dyn_cast<StoreInst>(inst)) {
      if (store->getValueOperand()->getType()->isPointerTy()) {
        // errs() << "[DEBUG] process StoreInst" << '\n';
        Value *ptr = store->getPointerOperand();
        for (PointerTy v : getValues(map, ptr)) {
          rewriteValue(map, v, store->getValueOperand());
        }
      }
    } else if (GetElementPtrInst *get = dyn_cast<GetElementPtrInst>(inst)) {
      if (get->getPointerOperandType()->isPointerTy()) {
        // errs() << "[DEBUG] process GetElementPtrInst" << '\n';
        Value *ptr = get->getPointerOperand();
        Value *idx = get->getOperand(get->getNumOperands() - 1);
        int64_t offset = dyn_cast<ConstantInt>(idx)->getSExtValue();
        // errs() << "offset: " << offset << '\n';
        for (PointerTy v : getValues(map, ptr)) {
          map[get].insert(PointerTy(v.first, offset));
        }
      }
    } else if (CastInst *cast = dyn_cast<CastInst>(inst)) {
      if (cast->getType()->isPointerTy()) {
        // errs() << "[DEBUG] process CastInst" << '\n';
        Value *ptr = cast->getOperand(0);
        copyValue(map, cast, ptr);
      }
    } else if (CallInst *call = dyn_cast<CallInst>(inst)) {
      // errs() << "[DEBUG] process CallInst" << '\n';
      Value *callee = call->getCalledOperand();
      std::set<PointerTy> singleCallee = {dyn_cast<Function>(callee)}, *callees;
      if (isa<Function>(callee)) {
        callees = &singleCallee;
      } else {
        callees = &map[callee];
      }
      for (PointerTy p : *callees) {
        Function *f = dyn_cast<Function>(p.first);
        if (!f)
          continue;
        // Ignore LLVM intrinsics
        if (f->getName().startswith("llvm."))
          continue;
        // Store callee in a special way
        map[PointerTy(call, PtrTyCall)].insert(f);
        // Ignore declaration
        if (f->isDeclaration())
          continue;
        DataflowResult<PointerMap>::Type subres;
        PointerMap initval = map; // copy context
        // Copy arguments
        for (unsigned int i = 0; i < call->getNumArgOperands(); i++) {
          Value *arg = call->getArgOperand(i);
          if (arg->getType()->isPointerTy()) {
            copyValue(initval, f->getArg(i), map, arg);
          }
        }
        // Compute dataflow for called function
        compForwardDataflow<PointerMap>(f, this, &subres, initval);
        // Merge callee context values
        PointerMap resmap;
        for (BasicBlock &BB : *f) {
          for (auto it : subres[&BB].second) {
            copyValue(resmap, it.first, subres[&BB].second, it.first);
          }
        }
        // errs() << "before (" << f->getName() << "):\n" << map << '\n';
        // errs() << "resmap (" << f->getName() << "):\n" << resmap << '\n';
        if (call->getType()->isPointerTy()) {
          copyValue(map, call, resmap, PointerTy(f, PtrTyReturn));
        }
        for (auto it : resmap) {
          if (it.first.second == PtrTyCall || it.first.second >= 0) {
            rewriteValue(map, it.first, resmap, it.first);
          }
        }
        // errs() << "after (" << f->getName() << "):\n" << map << '\n';
      }
    } else if (ReturnInst *ret = dyn_cast<ReturnInst>(inst)) {
      if (ret->getReturnValue() &&
          ret->getReturnValue()->getType()->isPointerTy()) {
        // errs() << "[DEBUG] process ReturnInst" << '\n';
        copyValue(map, PointerTy(ret->getFunction(), PtrTyReturn),
                  ret->getReturnValue());
      }
    }
  }
};

static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }

// Name unnamed values for debug purpose
struct NameAnonymousValuePass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  NameAnonymousValuePass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {
    for (Function &F : M) {
      processFunction(F);
    }
    return false;
  }

private:
  unsigned int mNumber = 0;

  void checkAndNameValue(Value &v) {
    if (!v.hasName() && !v.getType()->isVoidTy()) {
      v.setName("name" + std::to_string(mNumber++));
    }
  }

  void processFunction(Function &F) {
    for (Argument &A : F.args()) {
      checkAndNameValue(A);
    }
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB.getInstList()) {
        checkAndNameValue(I);
      }
    }
  }
};

char NameAnonymousValuePass::ID = 0;

struct EnableFunctionOptPass : public FunctionPass {
  static char ID;
  EnableFunctionOptPass() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override {
    if (F.hasFnAttribute(Attribute::OptimizeNone)) {
      F.removeFnAttr(Attribute::OptimizeNone);
    }
    return true;
  }
};

char EnableFunctionOptPass::ID = 0;

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 3
struct FuncPtrPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  FuncPtrPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {
    for (Function &F : M) {
      processFunction(F);
    }

    for (auto it : mFuncMap) {
      outs() << it.first << " : ";
      bool first = true;
      for (Function *f : it.second) {
        if (first) {
          first = false;
        } else {
          outs() << ", ";
        }
        outs() << f->getName();
      }
      outs() << '\n';
    }

    return false;
  }

  void processFunction(Function &F) {
    // Ignore declaration
    if (F.isDeclaration())
      return;

    // Ignore llvm intrinsics
    if (F.getName().startswith("llvm."))
      return;

    PointerVisitor visitor;
    DataflowResult<PointerMap>::Type result;
    PointerMap initval;
    compForwardDataflow<PointerMap>(&F, &visitor, &result, initval);
    // printDataflowResult<PointerMap>(outs(), result);

    PointerMap resmap;
    for (BasicBlock &BB : F) {
      for (auto it : result[&BB].second) {
        copyValue(resmap, it.first, result[&BB].second, it.first);
      }
    }

    for (auto it : resmap) {
      if (it.first.second == PtrTyCall) {
        CallInst *call = dyn_cast<CallInst>(it.first.first);
        for (PointerTy v : it.second) {
          mFuncMap[call->getDebugLoc().getLine()].insert(
              dyn_cast<Function>(v.first));
        }
      }
    }
  }

private:
  std::map<unsigned int, std::set<Function *>> mFuncMap;
};

char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass",
                                   "Print function call instruction");

struct DumpPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  DumpPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {
    outs() << M;
    return false;
  }
};

char DumpPass::ID = 0;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<filename>.bc"), cl::init(""));

static cl::opt<bool> DumpLL("dumpll", cl::desc("dump ll only"),
                            cl::init(false));

int main(int argc, char **argv) {
  LLVMContext &Context = getGlobalContext();
  SMDiagnostic Err;
  // Parse the command line to read the Inputfilename
  cl::ParseCommandLineOptions(
      argc, argv, "FuncPtrPass \n My first LLVM too which does not do much.\n");

  // Load the input module
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  llvm::legacy::PassManager Passes;
#if LLVM_VERSION_MAJOR >= 5
  Passes.add(new EnableFunctionOptPass());
#endif
  /// Transform it to SSA
  Passes.add(llvm::createPromoteMemoryToRegisterPass());

  Passes.add(new NameAnonymousValuePass());

  if (DumpLL) {
    Passes.add(new DumpPass());
  } else {
    Passes.add(new FuncPtrPass());
  }

  Passes.run(*M.get());
}
