#include "optimizer.hpp"

#include <utility>

void Optimizer::optimize(std::vector<std::unique_ptr<Stmt>> &program) {
  optimizeStmtList(program);
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
    if (expr->op == "&&" && !lhs->value) {
      return std::make_unique<BoolExpr>(false);
    }
    if (expr->op == "||" && lhs->value) {
      return std::make_unique<BoolExpr>(true);
    }

    if (auto *rhs = dynamic_cast<BoolExpr *>(expr->rhs.get())) {
      if (expr->op == "&&") {
        return std::make_unique<BoolExpr>(lhs->value && rhs->value);
      }
      if (expr->op == "||") {
        return std::make_unique<BoolExpr>(lhs->value || rhs->value);
      }
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

void Optimizer::optimizeStmtList(std::vector<std::unique_ptr<Stmt>> &stmts) {
  std::vector<std::unique_ptr<Stmt>> optimized;
  optimized.reserve(stmts.size());

  for (auto &stmt : stmts) {
    auto next = optimizeStmt(std::move(stmt));
    if (!next) {
      continue;
    }

    bool stopsExecution = dynamic_cast<ReturnStmt *>(next.get()) != nullptr;
    optimized.push_back(std::move(next));

    if (stopsExecution) {
      break;
    }
  }

  stmts = std::move(optimized);
}

std::unique_ptr<Stmt> Optimizer::optimizeStmt(std::unique_ptr<Stmt> stmt) {
  if (!stmt) {
    return nullptr;
  }

  if (auto *s = dynamic_cast<FuncStmt *>(stmt.get())) {
    s->body = optimizeStmt(std::move(s->body));
    if (!s->body) {
      s->body = std::make_unique<BlockStmt>(
          std::vector<std::unique_ptr<Stmt>>{});
    }
    return stmt;
  }

  if (auto *s = dynamic_cast<ReturnStmt *>(stmt.get())) {
    if (s->value) {
      s->value = optimizeExpr(std::move(s->value));
    }
    return stmt;
  }

  if (auto *s = dynamic_cast<VarStmt *>(stmt.get())) {
    if (s->init) {
      s->init = optimizeExpr(std::move(s->init));
    }
    return stmt;
  }

  if (auto *s = dynamic_cast<PrintStmt *>(stmt.get())) {
    s->expr = optimizeExpr(std::move(s->expr));
    return stmt;
  }

  if (auto *s = dynamic_cast<ExprStmt *>(stmt.get())) {
    s->expr = optimizeExpr(std::move(s->expr));
    return stmt;
  }

  if (auto *s = dynamic_cast<BlockStmt *>(stmt.get())) {
    optimizeStmtList(s->stmts);
    return stmt;
  }

  if (auto *s = dynamic_cast<IfStmt *>(stmt.get())) {
    s->cond = optimizeExpr(std::move(s->cond));

    s->thenBranch = optimizeStmt(std::move(s->thenBranch));
    if (!s->thenBranch) {
      s->thenBranch = std::make_unique<BlockStmt>(
          std::vector<std::unique_ptr<Stmt>>{});
    }

    if (s->elseBranch) {
      s->elseBranch = optimizeStmt(std::move(s->elseBranch));
    }

    if (auto *cond = dynamic_cast<BoolExpr *>(s->cond.get())) {
      if (cond->value) {
        return std::move(s->thenBranch);
      }
      return s->elseBranch ? std::move(s->elseBranch) : nullptr;
    }

    return stmt;
  }

  if (auto *s = dynamic_cast<WhileStmt *>(stmt.get())) {
    s->cond = optimizeExpr(std::move(s->cond));
    s->body = optimizeStmt(std::move(s->body));
    if (!s->body) {
      s->body = std::make_unique<BlockStmt>(
          std::vector<std::unique_ptr<Stmt>>{});
    }

    if (auto *cond = dynamic_cast<BoolExpr *>(s->cond.get())) {
      if (!cond->value) {
        return nullptr;
      }
    }

    return stmt;
  }

  return stmt;
}
