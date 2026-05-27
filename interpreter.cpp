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

Interpreter::Value Interpreter::Value::makeArray(std::size_t size) {
  Value x;
  x.type = TypeKind::Array;
  x.arrayValue.resize(size);
  x.elementType = TypeKind::Unknown;
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
  // Register functions before executing top-level statements. Function bodies
  // are executed only when the function is called.
  for (const auto &st : program) {
    if (auto *fn = dynamic_cast<const FuncStmt *>(st.get())) {
      functions_[fn->name] = fn;
    }
  }

  for (const auto &st : program) {
    if (dynamic_cast<const FuncStmt *>(st.get()))
      continue;
    executeStmt(st.get());
  }
}

void Interpreter::executeStmt(const Stmt *st) {
  if (dynamic_cast<const FuncStmt *>(st)) {
    return;
  }

  if (auto *s = dynamic_cast<const ReturnStmt *>(st)) {
    Value v = defaultValue();
    if (s->value)
      v = evalExpr(s->value.get());
    throw ReturnSignal{std::move(v)};
  }

  if (auto *s = dynamic_cast<const VarStmt *>(st)) {
    Value init = defaultValue();

    if (s->arraySize > 0) {
      init = Value::makeArray(s->arraySize);
      env_.define(s->name, std::move(init));
      return;
    }

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
    try {
      for (const auto &child : s->stmts) {
        executeStmt(child.get());
      }
      env_.endScope();
    } catch (...) {
      env_.endScope();
      throw;
    }
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

    if (v->type == TypeKind::Array) {
      throw std::runtime_error("runtime error: array '" + x->name +
                               "' cannot be used as a scalar value");
    }

    return *v;
  }

  if (auto *x = dynamic_cast<const CallExpr *>(e)) {
    return callFunction(x);
  }

  if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
    Value *array = env_.resolve(x->name);
    if (!array) {
      throw std::runtime_error("runtime error: undefined array '" + x->name +
                               "'");
    }

    if (array->type != TypeKind::Array) {
      throw std::runtime_error("runtime error: variable '" + x->name +
                               "' is not an array");
    }

    Value index = evalExpr(x->index.get());
    if (index.type != TypeKind::Int) {
      throw std::runtime_error("runtime error: array index must be int");
    }

    if (index.intValue < 0 ||
        static_cast<std::size_t>(index.intValue) >= array->arrayValue.size()) {
      throw std::runtime_error("runtime error: array index out of bounds");
    }

    const Value &slot =
        array->arrayValue[static_cast<std::size_t>(index.intValue)];

    if (slot.type == TypeKind::Unknown) {
      throw std::runtime_error(
          "runtime error: reading uninitialized array element");
    }

    return slot;
  }

  if (auto *x = dynamic_cast<const ArrayAssignExpr *>(e)) {
    Value *array = env_.resolve(x->name);
    if (!array) {
      throw std::runtime_error("runtime error: undefined array '" + x->name +
                               "'");
    }

    if (array->type != TypeKind::Array) {
      throw std::runtime_error("runtime error: variable '" + x->name +
                               "' is not an array");
    }

    Value index = evalExpr(x->index.get());
    if (index.type != TypeKind::Int) {
      throw std::runtime_error("runtime error: array index must be int");
    }

    if (index.intValue < 0 ||
        static_cast<std::size_t>(index.intValue) >= array->arrayValue.size()) {
      throw std::runtime_error("runtime error: array index out of bounds");
    }

    Value rhs = evalExpr(x->value.get());
    if (rhs.type == TypeKind::Array) {
      throw std::runtime_error(
          "runtime error: arrays cannot be stored inside arrays");
    }

    if (array->elementType == TypeKind::Unknown) {
      array->elementType = rhs.type;
    } else if (array->elementType != rhs.type) {
      throw std::runtime_error("runtime error: array element type mismatch");
    }

    array->arrayValue[static_cast<std::size_t>(index.intValue)] = rhs;
    return rhs;
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

Interpreter::Value Interpreter::callFunction(const CallExpr *call) {
  auto it = functions_.find(call->callee);
  if (it == functions_.end()) {
    throw std::runtime_error("runtime error: undefined function '" +
                             call->callee + "'");
  }

  const FuncStmt *fn = it->second;
  if (call->args.size() != fn->params.size()) {
    throw std::runtime_error("runtime error: function '" + call->callee +
                             "' expects " + std::to_string(fn->params.size()) +
                             " arguments, got " +
                             std::to_string(call->args.size()));
  }

  std::vector<Value> argValues;
  argValues.reserve(call->args.size());
  for (const auto &arg : call->args)
    argValues.push_back(evalExpr(arg.get()));

  env_.beginScope();
  try {
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
      env_.define(fn->params[i], std::move(argValues[i]));
    }

    if (auto *body = dynamic_cast<const BlockStmt *>(fn->body.get())) {
      for (const auto &child : body->stmts)
        executeStmt(child.get());
    } else {
      executeStmt(fn->body.get());
    }

    env_.endScope();
    return defaultValue();
  } catch (const ReturnSignal &ret) {
    env_.endScope();
    return ret.value;
  } catch (...) {
    env_.endScope();
    throw;
  }
}

std::string Interpreter::valueToString(const Value &v) {
  switch (v.type) {
  case TypeKind::Int:
    return std::to_string(v.intValue);
  case TypeKind::Bool:
    return v.boolValue ? "true" : "false";
  case TypeKind::Str:
    return v.strValue;
  case TypeKind::Array:
    return "<array>";
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
