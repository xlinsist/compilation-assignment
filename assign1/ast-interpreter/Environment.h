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

// Each StackFrame represents a function. StackFrame maps variable declaration,
// expressions and pointers to Value, which is represented in the form of either
// integer or addresses.
class StackFrame {
  std::map<Decl *, int64_t> mVars;
  std::map<Stmt *, int64_t> mExprs;
  std::map<Stmt *, int64_t *> mPtrs;
  // The return value of the function.
  int64_t returnValue;

public:
  StackFrame() : mVars(), mExprs(), mPtrs() {}

  // The following functions update or inquire the maps in StackFrame.
  void bindDecl(Decl *decl, int64_t val) {
    printf("debug bindDecl val = %ld\n", val);
    mVars[decl] = val;
  }
  int64_t getDeclVal(Decl *decl) {
    assert(mVars.find(decl) != mVars.end());
    printf("debug getDeclVal val = %ld\n", mVars.find(decl)->second);
    return mVars.find(decl)->second;
  }
  bool hasDecl(Decl *decl) { return (mVars.find(decl) != mVars.end()); }
  void bindStmt(Stmt *stmt, int64_t val) {
    printf("debug bindStmt val = %ld\n", val);
    mExprs[stmt] = val;
  }
  int64_t getStmtVal(Stmt *stmt) {
    if (mExprs.find(stmt) == mExprs.end()) {
      llvm::errs() << "Statement not found\n";
      stmt->dump();
      assert(false);
    }
    printf("debug getStmtVal val = %ld\n", mExprs[stmt]);
    return mExprs[stmt];
  }
  void bindPtr(Stmt *stmt, int64_t *val) {
    printf("debug bindPtr val = %ld\n", *val);
    mPtrs[stmt] = val;
  }
  int64_t *getPtr(Stmt *stmt) {
    assert(mPtrs.find(stmt) != mPtrs.end());
    return mPtrs[stmt];
  }
  void setReturnValue(int64_t value) { returnValue = value; }
  int64_t getReturnValue() { return returnValue; }
};

// Environment is where the procedure execute.
class Environment {
  std::vector<StackFrame> mStack;
  // Declartions to the built-in functions.
  FunctionDecl *mFree;
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;
  FunctionDecl *mEntry;
  // Maps for global variables.
  std::map<Decl *, int64_t> gVars;

public:
  // Get the declartions to the built-in functions.
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {
    // Initialize a temporary StackFrame to process global variables.
    mStack.push_back(StackFrame());
  }

  // `getExprValue` and `getEntry` are called by ASTInterpreter.cpp.
  int64_t getExprValue(Expr *expr) { return mStack.back().getStmtVal(expr); }
  FunctionDecl *getEntry() { return mEntry; }

  // Save the result of a function with return value.
  void returnStmt(Expr *retexpr) {
    int64_t returnValue = mStack.back().getStmtVal(retexpr);
    mStack.back().setReturnValue(returnValue);
  }

  // Initialize the Environment.
  void init(TranslationUnitDecl *unit) {
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      // Process functions.
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
      // Process global variables. They are directly stored in a map settled in
      // the Environment.
      else if (VarDecl *vardecl = dyn_cast<VarDecl>(*i)) {
        if (vardecl->hasInit()) {
          int64_t val = mStack.back().getStmtVal(vardecl->getInit());
          gVars[vardecl] = val;
        } else {
          gVars[vardecl] = 0;
        }
      }
    }
    // Delete the temporary StackFrame and start a new StackFrame.
    mStack.pop_back();
    mStack.push_back(StackFrame());
  }

  // Adding all literals into mStack to help procedures access them by
  // getStmtVal, for literals(the form of constant numbers) are seen as a kind
  // of expressions.
  void literal(Expr *expr) {
    if (IntegerLiteral *literal = dyn_cast<IntegerLiteral>(expr)) {
      mStack.back().bindStmt(expr, literal->getValue().getSExtValue());
    } else {
      llvm::errs() << "Unsupported literal\n";
      assert(false);
    }
  }

  // Deal with `sizeof` in a statement.
  void ueot(UnaryExprOrTypeTraitExpr *ueotexpr) {
    UnaryExprOrTypeTrait kind = ueotexpr->getKind();
    int64_t result = 0;
    if (kind == UETT_SizeOf) {
      result = sizeof(int64_t);
    } else {
      llvm::errs() << "Unsupported UEOT\n";
      assert(false);
    }
    mStack.back().bindStmt(ueotexpr, result);
  }

  // Deal with `*(a+1)` in a statement.
  void paren(ParenExpr *parenexpr) {
    mStack.back().bindStmt(parenexpr,
                           mStack.back().getStmtVal(parenexpr->getSubExpr()));
  }

  void binop(BinaryOperator *bop) {
    printf("debug binop\n");
    Expr *left = bop->getLHS();
    Expr *right = bop->getRHS();
    int64_t result = 0;
    int64_t rightValue = mStack.back().getStmtVal(right);

    if (bop->isAssignmentOp()) {
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        Decl *decl = declexpr->getFoundDecl();
        mStack.back().bindDecl(decl, rightValue);
      }
      // Deal with `*a = 1` situation.
      else if (UnaryOperator *uop = dyn_cast<UnaryOperator>(left)) {
        assert(uop->getOpcode() == UO_Deref);
        int64_t *ptr = mStack.back().getPtr(left);
        *ptr = rightValue;
      } else {
        llvm::errs() << "Unsupported left value type in binop\n";
        assert(false);
      }
      result = rightValue;
    } else {
      int64_t leftValue = mStack.back().getStmtVal(left);
      typedef BinaryOperatorKind Opcode;
      Opcode opc = bop->getOpcode();

      // In `*a+1` situation, the unit movement distance is sizeof(int64_t).
      if (left->getType()->isPointerType() &&
          right->getType()->isIntegerType()) {
        assert(opc == BO_Add || opc == BO_Sub);
        rightValue = rightValue * sizeof(int64_t);
      } else if (left->getType()->isIntegerType() &&
                 right->getType()->isPointerType()) {
        assert(opc == BO_Add || opc == BO_Sub);
        leftValue = leftValue * sizeof(int64_t);
      }
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
    int64_t value = mStack.back().getStmtVal(uop->getSubExpr());
    int64_t result = 0;
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
    else if (opc == UO_PostInc)
      result = value++;
    else if (opc == UO_PostDec)
      result = value--;
    else if (opc == UO_PreInc)
      result = ++value;
    else if (opc == UO_PreDec)
      result = --value;
    else if (opc == UO_Deref) {
      printf("debug start to bindPtr in unaryop\n");
      mStack.back().bindPtr(uop, (int64_t *)value);
      result = *(int64_t *)value;
    } else {
      llvm::errs() << "Unsupported operation in unaryop\n";
      assert(false);
    }
    mStack.back().bindStmt(uop, result);
  }

  void decl(DeclStmt *declstmt) {
    printf("debug decl\n");
    // We may declare multiple variables in the same statement like `int a, b`.
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
        QualType type = vardecl->getType();
        if (type->isIntegerType() || type->isPointerType()) {
          // Declare `int64_t a = 1` and `int64_t *a = MALLOC(4)` situations.
          if (vardecl->hasInit()) {
            int64_t val = mStack.back().getStmtVal(vardecl->getInit());
            mStack.back().bindDecl(vardecl, val);
          } else {
            // Declare `int64_t a` and `int64_t *a` situations and initialize
            // them to 0.
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

  // Bind the stmt of declref, becuase it is viewed as a kind of expressions.
  void declref(DeclRefExpr *declref) {
    printf("debug declref\n");
    QualType type = declref->getType();
    if (type->isIntegerType() || type->isPointerType()) {
      Decl *decl = declref->getFoundDecl();
      int64_t val;
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
      // Just do nothing.
    } else {
      llvm::errs() << "Unsupported declref type in declref\n";
      declref->dump();
      type->dump();
    }
  }

  // Deal with ImplicitCastExpr.
  void cast(CastExpr *castexpr) {
    printf("debug cast\n");
    QualType type = castexpr->getType();
    if (type->isIntegerType() ||
        (type->isPointerType() && !type->isFunctionPointerType())) {
      Expr *expr = castexpr->getSubExpr();
      int64_t val = mStack.back().getStmtVal(expr);
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
    int64_t returnValue = mStack.back().getReturnValue();
    mStack.pop_back();
    mStack.back().bindStmt(callexpr, returnValue);
  }

  // Judge if the function is a builtin function.
  bool builtinFunc(CallExpr *callexpr) {
    int64_t val = 0;
    FunctionDecl *callee = callexpr->getDirectCallee();
    if (callee == mInput) {
      llvm::errs() << "Please Input an Integer Value : ";
      scanf("%ld", &val);
      mStack.back().bindStmt(callexpr, val);
    } else if (callee == mOutput) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl);
      llvm::errs() << val;
      mStack.back().bindStmt(callexpr, 0);
    } else if (callee == mMalloc) {
      int64_t size = mStack.back().getStmtVal(callexpr->getArg(0));
      mStack.back().bindStmt(callexpr, reinterpret_cast<int64_t>(malloc(size)));
    } else if (callee == mFree) {
      int64_t *ptr = reinterpret_cast<int64_t *>(
          mStack.back().getStmtVal(callexpr->getArg(0)));
      free(ptr);
    } else {
      return false;
    }
    return true;
  }
};
