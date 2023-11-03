//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

// Use ReturnException as a signal to finish a function.
class ReturnException : public std::exception {};

class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor> {
public:
  explicit InterpreterVisitor(const ASTContext &context, Environment *env)
      : EvaluatedExprVisitor(context), mEnv(env) {}
  virtual ~InterpreterVisitor() {}

  virtual void VisitIntegerLiteral(IntegerLiteral *literal) {
    mEnv->literal(literal);
  }
  virtual void VisitBinaryOperator(BinaryOperator *bop) {
    VisitStmt(bop);
    mEnv->binop(bop);
  }
  virtual void VisitUnaryOperator(UnaryOperator *uop) {
    VisitStmt(uop);
    mEnv->unaryop(uop);
  }
  virtual void VisitDeclRefExpr(DeclRefExpr *expr) {
    VisitStmt(expr);
    mEnv->declref(expr);
  }
  virtual void VisitParenExpr(ParenExpr *parenexpr) {
    VisitStmt(parenexpr);
    mEnv->paren(parenexpr);
  }
  virtual void VisitCastExpr(CastExpr *expr) {
    VisitStmt(expr);
    mEnv->cast(expr);
  }
  virtual void VisitCallExpr(CallExpr *call) {
    VisitStmt(call);
    if (mEnv->builtinFunc(call)) {
      // No need to do extra work.
    } else {
      mEnv->enterFunc(call);
      try {
        VisitStmt(call->getDirectCallee()->getBody());
      } catch (ReturnException e) {
        // printf("debug a function is successfully processed\n");
      }
      mEnv->exitFunc(call);
    }
  }
  virtual void VisitReturnStmt(ReturnStmt *ret) {
    VisitStmt(ret);
    mEnv->returnStmt(ret->getRetValue());
    throw ReturnException();
  }
  virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *expr) {
    mEnv->ueot(expr);
  }
  virtual void VisitDeclStmt(DeclStmt *declstmt) {
    VisitStmt(declstmt);
    mEnv->decl(declstmt);
  }

  virtual void VisitIfStmt(IfStmt *ifstmt) {
    Expr *cond = ifstmt->getCond();
    Visit(cond);
    if (mEnv->getExprValue(cond)) {
      Visit(ifstmt->getThen());
    } else {
      if (Stmt *elseStmt = ifstmt->getElse()) {
        Visit(elseStmt);
      }
    }
  }
  virtual void VisitWhileStmt(WhileStmt *whilestmt) {
    Expr *cond = whilestmt->getCond();
    Stmt *body = whilestmt->getBody();
    Visit(cond);
    while (mEnv->getExprValue(cond)) {
      Visit(body);
      Visit(cond);
    }
  }
  virtual void VisitForStmt(ForStmt *forstmt) {
    Stmt *init = forstmt->getInit();
    Expr *cond = forstmt->getCond();
    Expr *inc = forstmt->getInc();
    Stmt *body = forstmt->getBody();
    Visit(init);
    Visit(cond);
    while (mEnv->getExprValue(cond)) {
      Visit(body);
      if (inc) {
        Visit(inc);
      }
      if (cond) {
        Visit(cond);
      }
    }
  }

private:
  Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
  explicit InterpreterConsumer(const ASTContext &context)
      : mEnv(), mVisitor(context, &mEnv) {}

  virtual ~InterpreterConsumer() {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    // TranslationUnitDecl is the top declaration context of the AST.
    TranslationUnitDecl *decl = Context.getTranslationUnitDecl();

    /// TODO: is there a way to remove the iteration below but also guarantee we
    /// will finish visiting all literals before we start to process global
    /// variables?
    // Process global variables specifically.
    for (TranslationUnitDecl::decl_iterator i = decl->decls_begin(),
                                            e = decl->decls_end();
         i != e; ++i) {
      if (VarDecl *vdecl = dyn_cast<VarDecl>(*i)) {
        if (vdecl->hasInit()) {
          mVisitor.Visit(vdecl->getInit());
        }
      }
    }

    mEnv.init(decl);

    FunctionDecl *entry = mEnv.getEntry();
    try {
      mVisitor.VisitStmt(entry->getBody());
    } catch (ReturnException e) {
      // printf("debug the main function is successfully processed\n");
    }
  }

private:
  Environment mEnv;
  InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main(int argc, char **argv) {
  if (argc > 1) {
    printf("debug start\n");
    clang::tooling::runToolOnCode(
        std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction),
        argv[1]);
  }
}
