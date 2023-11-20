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
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#include <iostream>

using namespace std;
using namespace llvm;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang
 * will have optnone attribute which would lead to some transform passes
 * disabled, like mem2reg.
 */
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

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
/// Updated 11/10/2017 by fargo: make all functions
/// processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  FuncPtrPass() : ModulePass(ID) {}
  set<Function *> funcSet;
  vector<set<Function *>> ansInLine;

  bool runOnModule(Module &M) override {
    for (auto it = M.begin(); it != M.end(); ++it) {
      if (it->isDeclaration())
        continue;
      funcSet.insert(&*it);
    }

    for (auto it = M.begin(); it != M.end(); ++it) {
      if (it->isDeclaration())
        continue;
      vector<set<Function *>> params;
      FunctionType *funT = it->getFunctionType();
      for (auto it1 = funT->param_begin(); it1 != funT->param_end(); ++it1) {
        set<Function *> param;
        params.push_back(param);
      }
      // Parsing all the functions.
      Parse(&*it, params, map<Value *, set<Function *>>());
    }
    for (unsigned i = 0; i < ansInLine.size(); i++) {
      // Get the functions that may be visited in line `i` of the procedure.
      set<Function *> &funcs = ansInLine[i];
      if (funcs.size()) {
        outs() << i << " : ";
        for (auto it = funcs.begin(); it != funcs.end(); ++it) {
          if (it != funcs.begin())
            outs() << ", ";
          outs() << (*it)->getName();
        }
        outs() << "\n";
      }
    }
    return false;
  }

  set<Function *> ParseCallInst(set<BasicBlock *> &parent, CallInst *callInst,
                                vector<set<Function *>> args,
                                map<Value *, set<Function *>> stackSet) {
    set<Function *> callees =
        ParseValue(parent, callInst->getCalledValue(), args, stackSet);
    vector<set<Function *>> argum;
    for (unsigned i = 0; i < callInst->getNumArgOperands(); i++) {
      Value *argi = callInst->getArgOperand(i);
      bool flag = argi->getType()->isPointerTy() &&
                  argi->getType()->getPointerElementType()->isFunctionTy();
      if (flag) {
        argum.push_back(ParseValue(parent, argi, args, stackSet));
      } else {
        argum.push_back(set<Function *>());
      }
    }
    set<Function *> s;
    for (auto calleeIt = callees.begin(); calleeIt != callees.end(); ++calleeIt) {
      Function *callee = *calleeIt;
      // Get the line by `getDebugLoc()`.
      unsigned line = callInst->getDebugLoc().getLine();
      while (ansInLine.size() <= callInst->getDebugLoc().getLine())
        ansInLine.push_back(set<Function *>());
      ansInLine[line].insert(callee);
      if (funcSet.count(callee) &&
          parent.count(&callee->getEntryBlock()) == 0) {
        set<Function *> returnSet = Parse(callee, argum, stackSet);
        for (auto it = returnSet.begin(); it != returnSet.end(); ++it) {
          s.insert(*it);
        }
      }
    }
    return s;
  }

  set<Function *> ParseValue(set<BasicBlock *> &parent, Value *val,
                             vector<set<Function *>> &args,
                             map<Value *, set<Function *>> &stackSet) {
    set<Function *> s;
    if (PHINode *phi = dyn_cast<PHINode>(val)) {
      for (unsigned i = 0; i < phi->getNumIncomingValues(); i++) {
        BasicBlock *block = phi->getIncomingBlock(i);
        if (parent.count(block)) {
          set<Function *> returnSet =
              ParseValue(parent, phi->getIncomingValue(i), args, stackSet);
          for (auto it = returnSet.begin(); it != returnSet.end(); ++it) {
            s.insert(*it);
          }
        }
      }
    } else if (Function *func = dyn_cast<Function>(val)) {
      s.insert(func);
    } else if (Argument *arg = dyn_cast<Argument>(val)) {
      set<Function *> &returnSet = args[arg->getArgNo()];
      for (auto it = returnSet.begin(); it != returnSet.end(); ++it) {
        s.insert(*it);
      }
    } else if (CallInst *callinst = dyn_cast<CallInst>(val)) {
      return ParseCallInst(parent, callinst, args, stackSet);
    } else {
      // assert(false);
    }
    return s;
  }
  void ParseBlock(BasicBlock *block, set<BasicBlock *> parent,
                  set<Function *> *ret, vector<set<Function *>> &args,
                  map<Value *, set<Function *>> stackSet) {
    parent.insert(block);

    for (auto it = block->begin(); it != block->end(); ++it) {
      if (dyn_cast<DbgInfoIntrinsic>(&*it)) {
        continue;
      }

      BranchInst *branch = dyn_cast<BranchInst>(&*it);
      if (branch) {
        auto sons = branch->successors();
        for (auto son = sons.begin(); son != sons.end(); ++son) {
          BasicBlock *b = *son;
          // If the son has not been visited, apply dfs.
          if (parent.count(b) == 0) {
            ParseBlock(b, parent, ret, args, stackSet);
          }
        }
      }
      // Parse `ret` instructions if the return value is a pointer.
      if (ReturnInst *setinst = dyn_cast<ReturnInst>(&*it)) {
        if (ret) {
          set<Function *> ret_funcs =
              ParseValue(parent, setinst->getReturnValue(), args, stackSet);
          // Accumulate all possible functions after pasing.
          for (auto it = ret_funcs.begin(); it != ret_funcs.end(); ++it) {
            ret->insert(*it);
          }
        }
      }
      // Parse `call` instructions.
      if (CallInst *callInst = dyn_cast<CallInst>(&*it)) {
        ParseCallInst(parent, callInst, args, stackSet);
      }
    }
  }
  set<Function *> Parse(Function *Fun, vector<set<Function *>> args,
                        map<Value *, set<Function *>> stackSet) {
    // Directly ignore the undfined functions.
    if (funcSet.count(Fun) == 0) {
      return set<Function *>();
    }
    // ret_ptr points to the possible return values if the function will return
    // pointers.
    set<Function *> ret, *ret_ptr = nullptr;
    if (Fun->getReturnType()->isPointerTy() &&
        Fun->getReturnType()->getPointerElementType()->isFunctionTy()) {
      ret_ptr = &ret;
    }
    // Parse the basic block of the functions.
    ParseBlock(&Fun->getEntryBlock(), set<BasicBlock *>(), ret_ptr, args,
               stackSet);

    return ret;
  }
};

char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass",
                                   "Print function call instruction");

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<filename>.bc"), cl::init(""));

int main(int argc, char **argv) {
  LLVMContext &Context = getGlobalContext();
  SMDiagnostic Err;
  // Parse the command line to read the Inputfilename
  cl::ParseCommandLineOptions(
      argc, argv, "FuncPtrPass \n My first LLVM too which does not do much.\n");

  // Load the input module
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(argv[0], outs());
    return 1;
  }

  llvm::legacy::PassManager Passes;

  /// Remove functions' optnone attribute in LLVM5.0
  Passes.add(new EnableFunctionOptPass());
  /// Transform it to SSA
  Passes.add(llvm::createPromoteMemoryToRegisterPass());

  /// Your pass to print Function and Call Instructions
  Passes.add(new FuncPtrPass());
  Passes.run(*M.get());
}
