#include "optimizer.hpp"

#include <utility>

void Optimizer::optimize(std::vector<std::unique_ptr<Stmt>> &program) {
  for (auto &stmt : program) {
    optimizeStmt(stmt.get());
  }
}

bool Optimizer::isLiteral(const Expr *expr) {
  return dynamic_cast<const NumberExpr *>(expr) ||
         dynamic_cast<const StringExpr *>(expr) ||
         dynamic_cast<const BoolExpr *>(expr);
}

std::unique_ptr<Expr> Optimizer::optimizeExpr(std::unique_ptr<Expr> expr) {
  if (!expr) {
    return nullptr;
  }

  if (isLiteral(expr.get()) || dynamic_cast<IdentExpr *>(expr.get())) {
    return expr;
  }

  if (auto *x = dynamic_cast<UnaryExpr *>(expr.get())) {
    x->rhs = optimizeExpr(std::move(x->rhs));

    auto owned =
        std::unique_ptr<UnaryExpr>(static_cast<UnaryExpr *>(expr.release()));

    return tryFoldUnary(std::move(owned));
  }

  if (auto *x = dynamic_cast<BinaryExpr *>(expr.get())) {
    x->lhs = optimizeExpr(std::move(x->lhs));
    x->rhs = optimizeExpr(std::move(x->rhs));

    auto owned =
        std::unique_ptr<BinaryExpr>(static_cast<BinaryExpr *>(expr.release()));

    return tryFoldBinary(std::move(owned));
  }

  if (auto *x = dynamic_cast<IndexExpr *>(expr.get())) {
    x->index = optimizeExpr(std::move(x->index));
    return expr;
  }

  if (auto *x = dynamic_cast<ArrayAssignExpr *>(expr.get())) {
    x->index = optimizeExpr(std::move(x->index));
    x->value = optimizeExpr(std::move(x->value));
    return expr;
  }

  if (auto *x = dynamic_cast<CallExpr *>(expr.get())) {
    for (auto &arg : x->args) {
      arg = optimizeExpr(std::move(arg));
    }
    return expr;
  }

  if (auto *x = dynamic_cast<AssignExpr *>(expr.get())) {
    x->value = optimizeExpr(std::move(x->value));
    return expr;
  }

  return expr;
}

std::unique_ptr<Expr> Optimizer::tryFoldUnary(std::unique_ptr<UnaryExpr> expr) {
  if (auto *rhs = dynamic_cast<NumberExpr *>(expr->rhs.get())) {
    if (expr->op == "-") {
      return std::make_unique<NumberExpr>(-rhs->value);
    }
  }

  if (auto *rhs = dynamic_cast<BoolExpr *>(expr->rhs.get())) {
    if (expr->op == "!") {
      return std::make_unique<BoolExpr>(!rhs->value);
    }
  }

  return expr;
}

std::unique_ptr<Expr>
Optimizer::tryFoldBinary(std::unique_ptr<BinaryExpr> expr) {
  if (auto *lhs = dynamic_cast<NumberExpr *>(expr->lhs.get())) {
    if (auto *rhs = dynamic_cast<NumberExpr *>(expr->rhs.get())) {
      const long long a = lhs->value;
      const long long b = rhs->value;

      if (expr->op == "+") {
        return std::make_unique<NumberExpr>(a + b);
      }
      if (expr->op == "-") {
        return std::make_unique<NumberExpr>(a - b);
      }
      if (expr->op == "*") {
        return std::make_unique<NumberExpr>(a * b);
      }
      if (expr->op == "/") {
        if (b == 0) {
          return expr;
        }
        return std::make_unique<NumberExpr>(a / b);
      }

      if (expr->op == "<") {
        return std::make_unique<BoolExpr>(a < b);
      }
      if (expr->op == "<=") {
        return std::make_unique<BoolExpr>(a <= b);
      }
      if (expr->op == ">") {
        return std::make_unique<BoolExpr>(a > b);
      }
      if (expr->op == ">=") {
        return std::make_unique<BoolExpr>(a >= b);
      }
      if (expr->op == "==") {
        return std::make_unique<BoolExpr>(a == b);
      }
      if (expr->op == "!=") {
        return std::make_unique<BoolExpr>(a != b);
      }
    }
  }

  if (auto *lhs = dynamic_cast<StringExpr *>(expr->lhs.get())) {
    if (auto *rhs = dynamic_cast<StringExpr *>(expr->rhs.get())) {
      if (expr->op == "+") {
        return std::make_unique<StringExpr>(lhs->value + rhs->value);
      }
      if (expr->op == "==") {
        return std::make_unique<BoolExpr>(lhs->value == rhs->value);
      }
      if (expr->op == "!=") {
        return std::make_unique<BoolExpr>(lhs->value != rhs->value);
      }
    }
  }

  if (auto *lhs = dynamic_cast<BoolExpr *>(expr->lhs.get())) {
    if (auto *rhs = dynamic_cast<BoolExpr *>(expr->rhs.get())) {
      if (expr->op == "==") {
        return std::make_unique<BoolExpr>(lhs->value == rhs->value);
      }
      if (expr->op == "!=") {
        return std::make_unique<BoolExpr>(lhs->value != rhs->value);
      }
    }
  }

  return expr;
}

void Optimizer::optimizeStmt(Stmt *stmt) {
  if (!stmt) {
    return;
  }

  if (auto *s = dynamic_cast<FuncStmt *>(stmt)) {
    optimizeStmt(s->body.get());
    return;
  }

  if (auto *s = dynamic_cast<ReturnStmt *>(stmt)) {
    if (s->value) {
      s->value = optimizeExpr(std::move(s->value));
    }
    return;
  }

  if (auto *s = dynamic_cast<VarStmt *>(stmt)) {
    if (s->init) {
      s->init = optimizeExpr(std::move(s->init));
    }
    return;
  }

  if (auto *s = dynamic_cast<PrintStmt *>(stmt)) {
    s->expr = optimizeExpr(std::move(s->expr));
    return;
  }

  if (auto *s = dynamic_cast<ExprStmt *>(stmt)) {
    s->expr = optimizeExpr(std::move(s->expr));
    return;
  }

  if (auto *s = dynamic_cast<BlockStmt *>(stmt)) {
    for (auto &child : s->stmts) {
      optimizeStmt(child.get());
    }
    return;
  }

  if (auto *s = dynamic_cast<IfStmt *>(stmt)) {
    s->cond = optimizeExpr(std::move(s->cond));
    optimizeStmt(s->thenBranch.get());
    if (s->elseBranch) {
      optimizeStmt(s->elseBranch.get());
    }
    return;
  }

  if (auto *s = dynamic_cast<WhileStmt *>(stmt)) {
    s->cond = optimizeExpr(std::move(s->cond));
    optimizeStmt(s->body.get());
    return;
  }
}
