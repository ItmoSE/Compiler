#pragma once

#include "ast.hpp"

#include <memory>
#include <vector>

class Optimizer {
public:
  void optimize(std::vector<std::unique_ptr<Stmt>> &program);

private:
  std::unique_ptr<Expr> optimizeExpr(std::unique_ptr<Expr> expr);
  std::unique_ptr<Stmt> optimizeStmt(std::unique_ptr<Stmt> stmt);
  void optimizeStmtList(std::vector<std::unique_ptr<Stmt>> &stmts);

  std::unique_ptr<Expr> tryFoldUnary(std::unique_ptr<UnaryExpr> expr);
  std::unique_ptr<Expr> tryFoldBinary(std::unique_ptr<BinaryExpr> expr);

  static bool isLiteral(const Expr *expr);
};
