//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

std::string stmtToString(Stmt *stmt) {
  clang::LangOptions lo;
  std::string out_str;
  llvm::raw_string_ostream outstream(out_str);
  stmt->printPretty(outstream, NULL, PrintingPolicy(lo));
  return out_str;
}

// std::string declToString(Decl *decl) {

//   // Get the AST context and source manager
//   clang::ASTContext& astContext = decl->getASTContext();
//   const clang::SourceManager& sourceManager = astContext.getSourceManager();

//   // Create a printing policy
//   clang::PrintingPolicy printingPolicy(astContext.getLangOpts());
//   printingPolicy.Indentation = 4;  // Set the indentation level as desired

//   // Create a raw string stream to capture the printed output
//   llvm::SmallString<128> printedOutput;
//   llvm::raw_svector_ostream outputStream(printedOutput);

//   // Create an AST printer
//   clang::ASTPrinter printer(outputStream, printingPolicy, &sourceManager);

//   // Print the declaration
//   printer.Visit(const_cast<clang::Decl*>(decl));

//   // Output the printed output
//   return printedOutput.str();
//   // llvm::outs() << printedOutput.str() << "\n";
// }


class StackFrame {
  /// StackFrame maps Variable Declaration to Value
  /// Which are either integer or addresses (also represented using an Integer
  /// value)
  std::map<Decl *, int> mVars;
  std::map<Stmt *, int> mExprs;
  /// The current stmt
  Stmt *mPC;

public:
  StackFrame() : mVars(), mExprs(), mPC() {}

  void bindDecl(Decl *decl, int val) {
    printf("debug bindDecl val = %d\n", val);
    mVars[decl] = val; 
  }
  int getDeclVal(Decl *decl) {
    assert(mVars.find(decl) != mVars.end());
    printf("debug getDeclVal val = %d\n", mVars.find(decl)->second);
    return mVars.find(decl)->second;
  }
  void bindStmt(Stmt *stmt, int val) { 
    printf("debug bindStmt val = %d\n", val);
    mExprs[stmt] = val; 
  }
  int getStmtVal(Stmt *stmt) {
    // printf("debug getStmtVal print stmt = %s\n", stmtToString(stmt).c_str());
    printf("debug getStmtVal\n");
    assert(mExprs.find(stmt) != mExprs.end());
    printf("debug getStmtVal val = %d\n",mExprs[stmt]);
    return mExprs[stmt];
  }
  void setPC(Stmt *stmt) { mPC = stmt; }
  Stmt *getPC() { return mPC; }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment {
  std::vector<StackFrame> mStack;

  FunctionDecl *mFree; /// Declartions to the built-in functions
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;

  FunctionDecl *mEntry;

public:
  /// Get the declartions to the built-in functions
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {}

  /// Initialize the Environment
  void init(TranslationUnitDecl *unit) {
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)) {
        if (fdecl->getName().equals("FREE"))
          mFree = fdecl;
        else if (fdecl->getName().equals("MALLOC"))
          mMalloc = fdecl;
        else if (fdecl->getName().equals("GET"))
          mInput = fdecl;
        else if (fdecl->getName().equals("PRINT")) {
          mOutput = fdecl;
        }
          
        else if (fdecl->getName().equals("main"))
          mEntry = fdecl;
      }
    }
    mStack.push_back(StackFrame());
  }

  FunctionDecl *getEntry() { return mEntry; }

  /// !TODO Support comparison operation
  void binop(BinaryOperator *bop) {
    printf("debug binop\n");
    Expr *left = bop->getLHS();
    Expr *right = bop->getRHS();

    if (bop->isAssignmentOp()) {
      int val = mStack.back().getStmtVal(right);
      // printf("debug binop isAssignmentOp get val = %d\n", val);
      mStack.back().bindStmt(left, val);
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        Decl *decl = declexpr->getFoundDecl();
        mStack.back().bindDecl(decl, val);
      }
    }
  }

  void decl(DeclStmt *declstmt) {
    printf("debug decl\n");
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {        
        mStack.back().bindDecl(vardecl, 0);
      }
    }
  }
  void declref(DeclRefExpr *declref) {
    printf("debug declref\n");
    mStack.back().setPC(declref);
    if (declref->getType()->isIntegerType()) {
      Decl *decl = declref->getFoundDecl();
      int val = mStack.back().getDeclVal(decl);
      mStack.back().bindStmt(declref, val);
    }
  }

  void cast(CastExpr *castexpr) {
    printf("debug cast\n");
    mStack.back().setPC(castexpr);
    if (castexpr->getType()->isIntegerType()) {
      Expr *expr = castexpr->getSubExpr();
      printf("debug cast\n");
      int val = mStack.back().getStmtVal(expr);
      mStack.back().bindStmt(castexpr, val);
    }
  }

  /// !TODO Support Function Call
  void call(CallExpr *callexpr) {
    printf("debug call\n");
    mStack.back().setPC(callexpr);
    int val = 0;
    FunctionDecl *callee = callexpr->getDirectCallee();
    if (callee == mInput) {
      llvm::errs() << "Please Input an Integer Value : ";
      scanf("%d", &val);

      mStack.back().bindStmt(callexpr, val);
    } else if (callee == mOutput) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl);
      llvm::errs() << val;
    } else {
      /// You could add your code here for Function call Return
    }
  }
};
