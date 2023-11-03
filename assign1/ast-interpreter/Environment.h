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

using namespace clang;

// std::string stmtToString(Stmt *stmt) {
//   clang::LangOptions lo;
//   std::string out_str;
//   llvm::raw_string_ostream outstream(out_str);
//   stmt->printPretty(outstream, NULL, PrintingPolicy(lo));
//   return out_str;
// }

class StackFrame {
  /// StackFrame maps Variable Declaration to Value
  /// Which are either integer or addresses (also represented using an Integer
  /// value)
  std::map<Decl *, int> mVars;
  std::map<Stmt *, int> mExprs;
  std::map<Stmt *, int *> mPtrs;
  int returnValue; // 保存当前栈帧的返回值，只考虑整数

public:
  StackFrame() : mVars(), mExprs(), mPtrs() {}

  void bindDecl(Decl *decl, int val) {
    printf("debug bindDecl val = %d\n", val);
    mVars[decl] = val;
  }
  int getDeclVal(Decl *decl) {
    assert(mVars.find(decl) != mVars.end());
    printf("debug getDeclVal val = %d\n", mVars.find(decl)->second);
    return mVars.find(decl)->second;
  }
  bool hasDecl(Decl *decl) { return (mVars.find(decl) != mVars.end()); }
  void bindStmt(Stmt *stmt, int val) {
    printf("debug bindStmt val = %d\n", val);
    mExprs[stmt] = val;
  }
  int getStmtVal(Stmt *stmt) {
    if (mExprs.find(stmt) == mExprs.end()) {
      llvm::errs() << "Statement not found\n";
      stmt->dump();
      assert(false);
    }
    printf("debug getStmtVal val = %d\n", mExprs[stmt]);
    return mExprs[stmt];
  }
  // void bindPtr(Stmt *stmt, int *val) {
  //   printf("debug bindPtr val = %d\n", *val);
  //   mPtrs[stmt] = val;
  // }
  // int *getPtr(Stmt *stmt) {
  //   assert(mPtrs.find(stmt) != mPtrs.end());
  //   return mPtrs[stmt];
  // }
  void setReturnValue(int value) { returnValue = value; }
  int getReturnValue() { return returnValue; }
};

class Environment {
  std::vector<StackFrame> mStack;
  // Declartions to the built-in functions.
  FunctionDecl *mFree;
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;
  FunctionDecl *mEntry;
  // Maps for global variables.
  std::map<Decl *, int> gVars;

public:
  /// Get the declartions to the built-in functions.
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {
    mStack.push_back(StackFrame());
  }

  int getExprValue(Expr *expr) { return mStack.back().getStmtVal(expr); }

  FunctionDecl *getEntry() { return mEntry; }

  void returnStmt(Expr *retexpr) {
    int returnValue = mStack.back().getStmtVal(retexpr);
    mStack.back().setReturnValue(returnValue);
  }

  // Initialize the Environment.
  void init(TranslationUnitDecl *unit) {
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      // The decls consists of TypedefDecl(unrelevant here), VarDecl and
      // FunctionDecl.
      if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)) {
        if (fdecl->getName().equals("FREE"))
          mFree = fdecl;
        else if (fdecl->getName().equals("MALLOC"))
          mMalloc = fdecl;
        else if (fdecl->getName().equals("GET"))
          mInput = fdecl;
        else if (fdecl->getName().equals("PRINT"))
          mOutput = fdecl;
        else if (fdecl->getName().equals("main"))
          mEntry = fdecl;
      }
      // Set global variables.
      else if (VarDecl *vardecl = dyn_cast<VarDecl>(*i)) {
        if (vardecl->hasInit()) {
          int val = mStack.back().getStmtVal(vardecl->getInit());
          gVars[vardecl] = val;
        } else {
          gVars[vardecl] = 0;
        }
      }
    }
    mStack.pop_back();
    mStack.push_back(StackFrame());
  }

  /// TODO: Why design like this?
  // Adding all literals into mStack to help procedures access them by getStmtVal.
  void literal(Expr *expr) {
    printf("debug literal begin\n");
    if (IntegerLiteral *literal = dyn_cast<IntegerLiteral>(expr)) {
      mStack.back().bindStmt(expr, literal->getValue().getSExtValue());
    } else {
      llvm::errs() << "Unsupported literal\n";
      assert(false);
    }
  }

  void binop(BinaryOperator *bop) {
    printf("debug binop\n");
    Expr *left = bop->getLHS();
    Expr *right = bop->getRHS();
    int result = 0;
    int rightValue = mStack.back().getStmtVal(right);

    if (bop->isAssignmentOp()) {
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        Decl *decl = declexpr->getFoundDecl();
        mStack.back().bindDecl(decl, rightValue);
      } else {
        llvm::errs() << "Unsupported left value type in binop\n";
        assert(false);
      }
      result = rightValue;
    } else {
      int leftValue = mStack.back().getStmtVal(left);
      typedef BinaryOperatorKind Opcode;
      Opcode opc = bop->getOpcode();
      if (opc == BO_Add)
        result = leftValue + rightValue;
      else if (opc == BO_Sub)
        result = leftValue - rightValue;
      else if (opc == BO_Mul)
        result = leftValue * rightValue;
      else if (opc == BO_Div)
        result = leftValue / rightValue;
      else if (opc == BO_EQ)
        result = leftValue == rightValue;
      else if (opc == BO_NE)
        result = leftValue != rightValue;
      else if (opc == BO_LT)
        result = leftValue < rightValue;
      else if (opc == BO_GT)
        result = leftValue > rightValue;
      else if (opc == BO_LE)
        result = leftValue <= rightValue;
      else if (opc == BO_GE)
        result = leftValue >= rightValue;
      else {
        llvm::errs() << "Unsupported operation in binop\n";
        assert(false);
      }
    }
    mStack.back().bindStmt(bop, result);
  }

  void unaryop(UnaryOperator *uop) {
    printf("debug unaryop\n");
    int value = mStack.back().getStmtVal(uop->getSubExpr());
    int result = 0;
    typedef UnaryOperatorKind Opcode;
    Opcode opc = uop->getOpcode();
    if (opc == UO_Plus)
      result = value;
    else if (opc == UO_Minus)
      result = -value;
    else if (opc == UO_Not)
      result = ~value;
    else if (opc == UO_LNot)
      result = !value;
    else {
      llvm::errs() << "Unsupported operation in unaryop\n";
      assert(false);
    }
    mStack.back().bindStmt(uop, result);
  }

  void decl(DeclStmt *declstmt) {
    printf("debug decl\n");
    // We may declare multiple variables in the same statement like `int a,
    // b;`
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
        QualType type = vardecl->getType();
        if (type->isIntegerType()) {
          // Declare the `int a = 1` and other situations.
          if (vardecl->hasInit()) {
            int val = mStack.back().getStmtVal(vardecl->getInit());
            mStack.back().bindDecl(vardecl, val);
          } else {
            // Declare the `int a` situation and initialize variables to 0.
            mStack.back().bindDecl(vardecl, 0);
          }
        } else {
          llvm::errs() << "Unsupported decl type in decl\n";
          declstmt->dump();
          type->dump();
          assert(false);
        }
      }
    }
  }

  /// TODO: where do DeclRefExpr come from
  /// TODO: can we just not bind the stmt again
  // Bind the stmt of decl.
  void declref(DeclRefExpr *declref) {
    printf("debug declref\n");
    QualType type = declref->getType();
    if (type->isIntegerType()) {
      Decl *decl = declref->getFoundDecl();
      int val;
      if (mStack.back().hasDecl(decl)) {
        // Get val from local variables.
        val = mStack.back().getDeclVal(decl);
      } else if (gVars.find(decl) != gVars.end()) {
        // Get val from global variables.
        val = gVars[decl];
      } else {
        llvm::errs() << "Undefined variable in declref\n";
        declref->dump();
        type->dump();
        assert(false);
      }
      mStack.back().bindStmt(declref, val);
    } else if (type->isFunctionProtoType()) {
      // Do nothing.
    } else {
      llvm::errs() << "Unsupported declref type in declref\n";
      declref->dump();
      type->dump();
    }
  }

  /// TODO: where do ImplicitCastExpr come from
  // Deal with ImplicitCastExpr.
  void cast(CastExpr *castexpr) {
    printf("debug cast\n");
    QualType type = castexpr->getType();
    if (type->isIntegerType()) {
      Expr *expr = castexpr->getSubExpr();
      int val = mStack.back().getStmtVal(expr);
      mStack.back().bindStmt(castexpr, val);
    } else if (type->isFunctionPointerType()) {
      // Just do nothing, since we can catch the function in `enterFunc`.
    } else {
      llvm::errs() << "Unsupported cast type in cast\n";
      castexpr->dump();
      type->dump();
      assert(false);
    }
  }

  // Create a StackFrame for new function call and declare the input params.
  void enterFunc(CallExpr *callexpr) {
    FunctionDecl *callee = callexpr->getDirectCallee();
    int paramCount = callee->getNumParams();
    StackFrame newFrame = StackFrame();
    for (int i = 0; i < paramCount; i++) {
      newFrame.bindDecl(callee->getParamDecl(i),
                        mStack.back().getStmtVal(callexpr->getArg(i)));
    }
    mStack.push_back(newFrame);
  }

  // Exit the previous function and bind the result to the current function.
  void exitFunc(CallExpr *callexpr) {
    int returnValue = mStack.back().getReturnValue();
    mStack.pop_back();
    mStack.back().bindStmt(callexpr, returnValue);
  }

  // Judge if the function is a builtin function.
  bool builtinFunc(CallExpr *callexpr) {
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
      mStack.back().bindStmt(callexpr, 0);
    } else if (callee == mMalloc) {
      // int size = mStack.back().getStmtVal(callexpr->getArg(0));
      // mStack.back().bindStmt(callexpr, (int)malloc(size));
      /// TODO:
      assert(false);
    } else if (callee == mFree) {
      // int64_t *ptr = (int64_t
      // *)mStack.back().getStmtVal(callexpr->getArg(0)); free(ptr);
      /// TODO:
      assert(false);
    } else {
      return false;
    }
    return true;
  }
};
