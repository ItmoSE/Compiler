#include "analyzer.hpp"

#include <iostream>
#include <utility>

void Analyzer::analyze(const std::vector<std::unique_ptr<Stmt>> &program) {
  beginScope();

  // First pass: register all top-level functions, so calls can reference
  // functions declared later in the file.
  for (const auto &st : program) {
    if (auto *fn = dynamic_cast<const FuncStmt *>(st.get())) {
      declareFunction(fn);
    }
  }

  for (const auto &st : program) {
    analyzeStmt(st.get());
  }

  for (const auto &[name, fn] : functions_) {
    if (!fn.used) {
      std::cerr << "Warning: unused function '" << name << "' declared at "
                << fn.declLoc.line << ":" << fn.declLoc.col << "\n";
      warnings_++;
    }
  }

  endScope();
}

int Analyzer::warningCount() const { return warnings_; }

int Analyzer::errorCount() const { return errors_; }

bool Analyzer::hasErrors() const { return errors_ > 0; }

void Analyzer::beginScope() { scopes_.push_back({}); }

void Analyzer::endScope() {
  if (scopes_.empty()) {
    return;
  }

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

void Analyzer::reportError(SourceLoc loc, const std::string &msg) {
  std::cerr << "Error: " << msg << " at " << loc.line << ":" << loc.col << "\n";
  errors_++;
}

void Analyzer::declareVar(const std::string &name, SourceLoc loc,
                          TypeKind type) {
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

void Analyzer::declareFunction(const FuncStmt *fn) {
  auto it = functions_.find(fn->name);
  if (it != functions_.end()) {
    std::cerr << "Error: redeclaration of function '" << fn->name << "' at "
              << fn->loc.line << ":" << fn->loc.col
              << " (previous declaration at " << it->second.declLoc.line << ":"
              << it->second.declLoc.col << ")\n";
    errors_++;
    return;
  }

  functions_.emplace(fn->name, FunctionSymbol{fn->loc, fn->params.size(),
                                              TypeKind::Unknown, false});
}

Analyzer::Symbol *Analyzer::resolve(const std::string &name) {
  for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
    auto it = scopes_[i].find(name);
    if (it != scopes_[i].end()) {
      return &it->second;
    }
  }
  return nullptr;
}

TypeKind Analyzer::markUsedAndGetType(const std::string &name,
                                      SourceLoc useLoc) {
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

void Analyzer::assignTo(const std::string &name, SourceLoc loc,
                        TypeKind rhsType) {
  Symbol *sym = resolve(name);
  if (!sym) {
    std::cerr << "Error: assignment to undeclared variable '" << name << "' at "
              << loc.line << ":" << loc.col << "\n";
    errors_++;
    return;
  }

  if (rhsType == TypeKind::Error) {
    return;
  }

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

bool Analyzer::isComparisonOp(const std::string &op) const {
  return op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool Analyzer::isEqualityOp(const std::string &op) const {
  return op == "==" || op == "!=";
}

TypeKind Analyzer::analyzeExpr(const Expr *e) {
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
      if (rhs != TypeKind::Int && rhs != TypeKind::Error &&
          rhs != TypeKind::Unknown) {
        reportError(SourceLoc{},
                    "unary '-' expects int, got " + std::string(typeName(rhs)));
        return TypeKind::Error;
      }
      return rhs == TypeKind::Error ? TypeKind::Error : TypeKind::Int;
    }

    if (x->op == "!") {
      if (rhs != TypeKind::Bool && rhs != TypeKind::Error &&
          rhs != TypeKind::Unknown) {
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

    if (lhs == TypeKind::Error || rhs == TypeKind::Error) {
      return TypeKind::Error;
    }

    if (x->op == "+" || x->op == "-" || x->op == "*" || x->op == "/") {
      if (x->op == "+") {
        if (lhs == TypeKind::Int && rhs == TypeKind::Int)
          return TypeKind::Int;
        if (lhs == TypeKind::Str && rhs == TypeKind::Str)
          return TypeKind::Str;
        if (lhs == TypeKind::Unknown || rhs == TypeKind::Unknown) {
          if (lhs == TypeKind::Str || rhs == TypeKind::Str)
            return TypeKind::Str;
          if (lhs == TypeKind::Int || rhs == TypeKind::Int)
            return TypeKind::Int;
          return TypeKind::Unknown;
        }

        reportBinaryTypeError(x->op, lhs, rhs);
        return TypeKind::Error;
      }

      if ((lhs == TypeKind::Int || lhs == TypeKind::Unknown) &&
          (rhs == TypeKind::Int || rhs == TypeKind::Unknown))
        return TypeKind::Int;

      reportBinaryTypeError(x->op, lhs, rhs);
      return TypeKind::Error;
    }

    if (isComparisonOp(x->op)) {
      if ((lhs == TypeKind::Int || lhs == TypeKind::Unknown) &&
          (rhs == TypeKind::Int || rhs == TypeKind::Unknown))
        return TypeKind::Bool;

      reportBinaryTypeError(x->op, lhs, rhs);
      return TypeKind::Error;
    }

    if (isEqualityOp(x->op)) {
      if (lhs == rhs || lhs == TypeKind::Unknown || rhs == TypeKind::Unknown)
        return TypeKind::Bool;

      reportBinaryTypeError(x->op, lhs, rhs);
      return TypeKind::Error;
    }

    reportError(SourceLoc{}, "unknown binary operator '" + x->op + "'");
    return TypeKind::Error;
  }

  if (auto *x = dynamic_cast<const CallExpr *>(e)) {
    auto it = functions_.find(x->callee);
    if (it == functions_.end()) {
      reportError(x->loc, "call to undeclared function '" + x->callee + "'");
      for (const auto &arg : x->args)
        analyzeExpr(arg.get());
      return TypeKind::Error;
    }

    it->second.used = true;

    if (x->args.size() != it->second.arity) {
      reportError(x->loc, "function '" + x->callee + "' expects " +
                              std::to_string(it->second.arity) +
                              " arguments, got " +
                              std::to_string(x->args.size()));
    }

    for (const auto &arg : x->args)
      analyzeExpr(arg.get());

    return it->second.returnType;
  }

  if (auto *x = dynamic_cast<const AssignExpr *>(e)) {
    TypeKind valueType = analyzeExpr(x->value.get());
    assignTo(x->name, x->loc, valueType);
    Symbol *sym = resolve(x->name);
    return sym ? sym->type : TypeKind::Error;
  }

  return TypeKind::Error;
}

void Analyzer::reportBinaryTypeError(const std::string &op, TypeKind lhs,
                                     TypeKind rhs) {
  std::cerr << "Error: operator '" << op << "' cannot be applied to types '"
            << typeName(lhs) << "' and '" << typeName(rhs) << "'\n";
  errors_++;
}

void Analyzer::analyzeStmt(const Stmt *st) {
  if (auto *s = dynamic_cast<const FuncStmt *>(st)) {
    auto it = functions_.find(s->name);
    if (it == functions_.end()) {
      declareFunction(s);
      it = functions_.find(s->name);
      if (it == functions_.end())
        return;
    }

    FunctionSymbol *previousFunction = currentFunction_;
    currentFunction_ = &it->second;
    functionDepth_++;

    beginScope();
    for (const auto &param : s->params) {
      declareVar(param, s->loc, TypeKind::Unknown);
    }

    if (auto *body = dynamic_cast<const BlockStmt *>(s->body.get())) {
      for (const auto &child : body->stmts) {
        analyzeStmt(child.get());
      }
    } else {
      analyzeStmt(s->body.get());
    }

    endScope();
    functionDepth_--;
    currentFunction_ = previousFunction;
    return;
  }

  if (auto *s = dynamic_cast<const ReturnStmt *>(st)) {
    if (functionDepth_ == 0 || currentFunction_ == nullptr) {
      reportError(s->loc, "return outside function");
      if (s->value)
        analyzeExpr(s->value.get());
      return;
    }

    TypeKind retType = TypeKind::Unknown;
    if (s->value)
      retType = analyzeExpr(s->value.get());

    if (retType == TypeKind::Error)
      return;

    if (currentFunction_->returnType == TypeKind::Unknown) {
      currentFunction_->returnType = retType;
    } else if (currentFunction_->returnType != retType) {
      reportError(s->loc,
                  "inconsistent return type: previous return was '" +
                      std::string(typeName(currentFunction_->returnType)) +
                      "', current return is '" +
                      std::string(typeName(retType)) + "'");
    }
    return;
  }

  if (auto *s = dynamic_cast<const VarStmt *>(st)) {
    TypeKind initType = TypeKind::Unknown;
    if (s->init) {
      initType = analyzeExpr(s->init.get());
    }
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
    if (s->elseBranch) {
      analyzeStmt(s->elseBranch.get());
    }
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
