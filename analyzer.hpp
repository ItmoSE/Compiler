#pragma once

#include "ast.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Analyzer {
public:
  struct Symbol {
    SourceLoc declLoc;
    TypeKind type = TypeKind::Unknown;
    bool used = false;
  };

  void analyze(const std::vector<std::unique_ptr<Stmt>> &program) {
    beginScope();
    for (const auto &st : program) {
      analyzeStmt(st.get());
    }
    endScope();
  }

  int warningCount() const { return warnings_; }

private:
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  int warnings_ = 0;
  int errors_ = 0;

  void beginScope() { scopes_.push_back({}); }

  void endScope() {
    if (scopes_.empty())
      return;

    const auto &scope = scopes_.back();
    for (const auto &[name, sym] : scope) {
      if (!sym.used) {
        std::cerr << "Warning: unused variable '" << name << "' declared at "
                  << sym.declLoc.line << ":" << sym.declLoc.col << "\n";
        warnings_++;
      }
    }

    scopes_.pop_back();
  }

  void reportError(SourceLoc loc, const std::string &msg) {
    std::cerr << "Error: " << msg << " at " << loc.line << ":" << loc.col
              << "\n";
    errors_++;
  }

  void declareVar(const std::string &name, SourceLoc loc, TypeKind type) {
    auto &scope = scopes_.back();

    auto it = scope.find(name);
    if (it != scope.end()) {
      std::cerr << "Error: redeclaration of variable '" << name << "' at "
                << loc.line << ":" << loc.col << " (previous declaration at "
                << it->second.declLoc.line << ":" << it->second.declLoc.col
                << ")\n";
      errors_++;
      return;
    }

    scope.emplace(name, Symbol{loc, type, false});
  }

  Symbol *resolve(const std::string &name) {
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
      auto it = scopes_[i].find(name);
      if (it != scopes_[i].end()) {
        return &it->second;
      }
    }
    return nullptr;
  }

  TypeKind markUsedAndGetType(const std::string &name, SourceLoc useLoc) {
    Symbol *sym = resolve(name);
    if (!sym) {
      std::cerr << "Error: use of undeclared variable '" << name << "' at "
                << useLoc.line << ":" << useLoc.col << "\n";
      errors_++;
      return TypeKind::Error;
    }
    sym->used = true;
    return sym->type;
  }

  void assignTo(const std::string &name, SourceLoc loc, TypeKind rhsType) {
    Symbol *sym = resolve(name);
    if (!sym) {
      std::cerr << "Error: assignment to undeclared variable '" << name
                << "' at " << loc.line << ":" << loc.col << "\n";
      errors_++;
      return;
    }

    if (rhsType == TypeKind::Error)
      return;

    if (sym->type == TypeKind::Unknown) {
      sym->type = rhsType;
      return;
    }

    if (sym->type != rhsType) {
      std::cerr << "Error: cannot assign value of type '" << typeName(rhsType)
                << "' to variable '" << name << "' of type '"
                << typeName(sym->type) << "' at " << loc.line << ":" << loc.col
                << "\n";
      errors_++;
    }
  }

  bool isComparisonOp(const std::string &op) const {
    return op == "<" || op == "<=" || op == ">" || op == ">=";
  }

  bool isEqualityOp(const std::string &op) const {
    return op == "==" || op == "!=";
  }

  TypeKind analyzeExpr(const Expr *e) {
    if (dynamic_cast<const NumberExpr *>(e)) {
      return TypeKind::Int;
    }

    if (dynamic_cast<const StringExpr *>(e)) {
      return TypeKind::Str;
    }

    if (dynamic_cast<const BoolExpr *>(e)) {
      return TypeKind::Bool;
    }

    if (auto *x = dynamic_cast<const IdentExpr *>(e)) {
      return markUsedAndGetType(x->name, x->loc);
    }

    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) {
      TypeKind rhs = analyzeExpr(x->rhs.get());

      if (x->op == "-") {
        if (rhs != TypeKind::Int && rhs != TypeKind::Error) {
          reportError(SourceLoc{}, "unary '-' expects int, got " +
                                       std::string(typeName(rhs)));
          return TypeKind::Error;
        }
        return rhs == TypeKind::Error ? TypeKind::Error : TypeKind::Int;
      }

      if (x->op == "!") {
        if (rhs != TypeKind::Bool && rhs != TypeKind::Error) {
          reportError(SourceLoc{}, "unary '!' expects bool, got " +
                                       std::string(typeName(rhs)));
          return TypeKind::Error;
        }
        return rhs == TypeKind::Error ? TypeKind::Error : TypeKind::Bool;
      }

      reportError(SourceLoc{}, "unknown unary operator '" + x->op + "'");
      return TypeKind::Error;
    }

    if (auto *x = dynamic_cast<const BinaryExpr *>(e)) {
      TypeKind lhs = analyzeExpr(x->lhs.get());
      TypeKind rhs = analyzeExpr(x->rhs.get());

      if (lhs == TypeKind::Error || rhs == TypeKind::Error)
        return TypeKind::Error;

      if (x->op == "+" || x->op == "-" || x->op == "*" || x->op == "/") {
        if (x->op == "+") {
          if (lhs == TypeKind::Int && rhs == TypeKind::Int)
            return TypeKind::Int;
          if (lhs == TypeKind::Str && rhs == TypeKind::Str)
            return TypeKind::Str;

          reportBinaryTypeError(x->op, lhs, rhs);
          return TypeKind::Error;
        }

        if (lhs == TypeKind::Int && rhs == TypeKind::Int)
          return TypeKind::Int;

        reportBinaryTypeError(x->op, lhs, rhs);
        return TypeKind::Error;
      }

      if (isComparisonOp(x->op)) {
        if (lhs == TypeKind::Int && rhs == TypeKind::Int)
          return TypeKind::Bool;

        reportBinaryTypeError(x->op, lhs, rhs);
        return TypeKind::Error;
      }

      if (isEqualityOp(x->op)) {
        if (lhs == rhs)
          return TypeKind::Bool;

        reportBinaryTypeError(x->op, lhs, rhs);
        return TypeKind::Error;
      }

      reportError(SourceLoc{}, "unknown binary operator '" + x->op + "'");
      return TypeKind::Error;
    }

    if (auto *x = dynamic_cast<const AssignExpr *>(e)) {
      TypeKind valueType = analyzeExpr(x->value.get());
      assignTo(x->name, x->loc, valueType);
      Symbol *sym = resolve(x->name);
      return sym ? sym->type : TypeKind::Error;
    }

    return TypeKind::Error;
  }

  void reportBinaryTypeError(const std::string &op, TypeKind lhs,
                             TypeKind rhs) {
    std::cerr << "Error: operator '" << op << "' cannot be applied to types '"
              << typeName(lhs) << "' and '" << typeName(rhs) << "'\n";
    errors_++;
  }

  void analyzeStmt(const Stmt *st) {
    if (auto *s = dynamic_cast<const VarStmt *>(st)) {
      TypeKind initType = TypeKind::Unknown;
      if (s->init)
        initType = analyzeExpr(s->init.get());
      declareVar(s->name, s->loc, initType);
      return;
    }

    if (auto *s = dynamic_cast<const PrintStmt *>(st)) {
      analyzeExpr(s->expr.get());
      return;
    }

    if (auto *s = dynamic_cast<const ExprStmt *>(st)) {
      analyzeExpr(s->expr.get());
      return;
    }

    if (auto *s = dynamic_cast<const BlockStmt *>(st)) {
      beginScope();
      for (const auto &child : s->stmts) {
        analyzeStmt(child.get());
      }
      endScope();
      return;
    }

    if (auto *s = dynamic_cast<const IfStmt *>(st)) {
      TypeKind condType = analyzeExpr(s->cond.get());
      if (condType != TypeKind::Bool && condType != TypeKind::Error) {
        std::cerr << "Error: if condition must be bool\n";
        errors_++;
      }
      analyzeStmt(s->thenBranch.get());
      if (s->elseBranch)
        analyzeStmt(s->elseBranch.get());
      return;
    }

    if (auto *s = dynamic_cast<const WhileStmt *>(st)) {
      TypeKind condType = analyzeExpr(s->cond.get());
      if (condType != TypeKind::Bool && condType != TypeKind::Error) {
        std::cerr << "Error: while condition must be bool\n";
        errors_++;
      }
      analyzeStmt(s->body.get());
      return;
    }
  }
};
