#include "interpreter.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

Interpreter::Value Interpreter::Value::makeInt(long long v) {
  Value x;
  x.type = TypeKind::Int;
  x.intValue = v;
  return x;
}

Interpreter::Value Interpreter::Value::makeBool(bool v) {
  Value x;
  x.type = TypeKind::Bool;
  x.boolValue = v;
  return x;
}

Interpreter::Value Interpreter::Value::makeStr(std::string v) {
  Value x;
  x.type = TypeKind::Str;
  x.strValue = std::move(v);
  return x;
}

Interpreter::RuntimeEnvironment::RuntimeEnvironment() { beginScope(); }

void Interpreter::RuntimeEnvironment::beginScope() { scopes_.push_back({}); }

void Interpreter::RuntimeEnvironment::endScope() {
  if (scopes_.empty()) {
    throw std::runtime_error("runtime error: no scope to pop");
  }
  scopes_.pop_back();
}

void Interpreter::RuntimeEnvironment::define(const std::string &name,
                                             Value value) {
  if (scopes_.empty()) {
    throw std::runtime_error("runtime error: no active scope");
  }

  auto &scope = scopes_.back();
  auto [it, inserted] = scope.emplace(name, std::move(value));
  if (!inserted) {
    throw std::runtime_error("runtime error: redeclaration of variable '" +
                             name + "'");
  }
}

Interpreter::Value *
Interpreter::RuntimeEnvironment::resolve(const std::string &name) {
  for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
    auto it = scopes_[i].find(name);
    if (it != scopes_[i].end()) {
      return &it->second;
    }
  }
  return nullptr;
}

const Interpreter::Value *
Interpreter::RuntimeEnvironment::resolve(const std::string &name) const {
  for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
    auto it = scopes_[i].find(name);
    if (it != scopes_[i].end()) {
      return &it->second;
    }
  }
  return nullptr;
}

void Interpreter::RuntimeEnvironment::assign(const std::string &name,
                                             Value value) {
  Value *slot = resolve(name);
  if (!slot) {
    throw std::runtime_error(
        "runtime error: assignment to undefined variable '" + name + "'");
  }
  *slot = std::move(value);
}

Interpreter::Interpreter() = default;

void Interpreter::execute(const std::vector<std::unique_ptr<Stmt>> &program) {
  for (const auto &st : program) {
    executeStmt(st.get());
  }
}

void Interpreter::executeStmt(const Stmt *st) {
  if (auto *s = dynamic_cast<const VarStmt *>(st)) {
    Value init = defaultValue();
    if (s->init) {
      init = evalExpr(s->init.get());
    }
    env_.define(s->name, std::move(init));
    return;
  }

  if (auto *s = dynamic_cast<const PrintStmt *>(st)) {
    Value v = evalExpr(s->expr.get());
    std::cout << valueToString(v) << "\n";
    return;
  }

  if (auto *s = dynamic_cast<const ExprStmt *>(st)) {
    (void)evalExpr(s->expr.get());
    return;
  }

  if (auto *s = dynamic_cast<const BlockStmt *>(st)) {
    env_.beginScope();
    for (const auto &child : s->stmts) {
      executeStmt(child.get());
    }
    env_.endScope();
    return;
  }

  if (auto *s = dynamic_cast<const IfStmt *>(st)) {
    Value cond = evalExpr(s->cond.get());
    if (cond.type != TypeKind::Bool) {
      throw std::runtime_error("runtime error: if condition is not bool");
    }

    if (cond.boolValue) {
      executeStmt(s->thenBranch.get());
    } else if (s->elseBranch) {
      executeStmt(s->elseBranch.get());
    }
    return;
  }

  if (auto *s = dynamic_cast<const WhileStmt *>(st)) {
    while (true) {
      Value cond = evalExpr(s->cond.get());
      if (cond.type != TypeKind::Bool) {
        throw std::runtime_error("runtime error: while condition is not bool");
      }

      if (!cond.boolValue) {
        break;
      }

      executeStmt(s->body.get());
    }
    return;
  }

  throw std::runtime_error("runtime error: unknown statement node");
}

Interpreter::Value Interpreter::evalExpr(const Expr *e) {
  if (auto *x = dynamic_cast<const NumberExpr *>(e)) {
    return Value::makeInt(x->value);
  }

  if (auto *x = dynamic_cast<const StringExpr *>(e)) {
    return Value::makeStr(x->value);
  }

  if (auto *x = dynamic_cast<const BoolExpr *>(e)) {
    return Value::makeBool(x->value);
  }

  if (auto *x = dynamic_cast<const IdentExpr *>(e)) {
    const Value *v = env_.resolve(x->name);
    if (!v) {
      throw std::runtime_error("runtime error: undefined variable '" + x->name +
                               "'");
    }
    return *v;
  }

  if (auto *x = dynamic_cast<const AssignExpr *>(e)) {
    Value rhs = evalExpr(x->value.get());
    env_.assign(x->name, rhs);
    return rhs;
  }

  if (auto *x = dynamic_cast<const UnaryExpr *>(e)) {
    Value rhs = evalExpr(x->rhs.get());

    if (x->op == "-") {
      if (rhs.type != TypeKind::Int) {
        throw std::runtime_error("runtime error: unary '-' expects int");
      }
      return Value::makeInt(-rhs.intValue);
    }

    if (x->op == "!") {
      if (rhs.type != TypeKind::Bool) {
        throw std::runtime_error("runtime error: unary '!' expects bool");
      }
      return Value::makeBool(!rhs.boolValue);
    }

    throw std::runtime_error("runtime error: unknown unary operator '" + x->op +
                             "'");
  }

  if (auto *x = dynamic_cast<const BinaryExpr *>(e)) {
    Value lhs = evalExpr(x->lhs.get());
    Value rhs = evalExpr(x->rhs.get());

    if (x->op == "+") {
      if (lhs.type == TypeKind::Int && rhs.type == TypeKind::Int) {
        return Value::makeInt(lhs.intValue + rhs.intValue);
      }
      if (lhs.type == TypeKind::Str && rhs.type == TypeKind::Str) {
        return Value::makeStr(lhs.strValue + rhs.strValue);
      }
      throw std::runtime_error("runtime error: invalid operands for '+'");
    }

    if (x->op == "-") {
      requireInts(lhs, rhs, "-");
      return Value::makeInt(lhs.intValue - rhs.intValue);
    }

    if (x->op == "*") {
      requireInts(lhs, rhs, "*");
      return Value::makeInt(lhs.intValue * rhs.intValue);
    }

    if (x->op == "/") {
      requireInts(lhs, rhs, "/");
      if (rhs.intValue == 0) {
        throw std::runtime_error("runtime error: division by zero");
      }
      return Value::makeInt(lhs.intValue / rhs.intValue);
    }

    if (x->op == "<") {
      requireInts(lhs, rhs, "<");
      return Value::makeBool(lhs.intValue < rhs.intValue);
    }

    if (x->op == "<=") {
      requireInts(lhs, rhs, "<=");
      return Value::makeBool(lhs.intValue <= rhs.intValue);
    }

    if (x->op == ">") {
      requireInts(lhs, rhs, ">");
      return Value::makeBool(lhs.intValue > rhs.intValue);
    }

    if (x->op == ">=") {
      requireInts(lhs, rhs, ">=");
      return Value::makeBool(lhs.intValue >= rhs.intValue);
    }

    if (x->op == "==") {
      return Value::makeBool(equals(lhs, rhs));
    }

    if (x->op == "!=") {
      return Value::makeBool(!equals(lhs, rhs));
    }

    throw std::runtime_error("runtime error: unknown binary operator '" +
                             x->op + "'");
  }

  throw std::runtime_error("runtime error: unknown expression node");
}

std::string Interpreter::valueToString(const Value &v) {
  switch (v.type) {
  case TypeKind::Int:
    return std::to_string(v.intValue);
  case TypeKind::Bool:
    return v.boolValue ? "true" : "false";
  case TypeKind::Str:
    return v.strValue;
  default:
    return "<unknown>";
  }
}

void Interpreter::requireInts(const Value &lhs, const Value &rhs,
                              const std::string &op) {
  if (lhs.type != TypeKind::Int || rhs.type != TypeKind::Int) {
    throw std::runtime_error("runtime error: operator '" + op +
                             "' expects int operands");
  }
}

bool Interpreter::equals(const Value &lhs, const Value &rhs) {
  if (lhs.type != rhs.type) {
    throw std::runtime_error(
        "runtime error: '=='/'!=' require operands of the same type");
  }

  switch (lhs.type) {
  case TypeKind::Int:
    return lhs.intValue == rhs.intValue;
  case TypeKind::Bool:
    return lhs.boolValue == rhs.boolValue;
  case TypeKind::Str:
    return lhs.strValue == rhs.strValue;
  default:
    throw std::runtime_error("runtime error: cannot compare unknown values");
  }
}

Interpreter::Value Interpreter::defaultValue() { return Value{}; }
