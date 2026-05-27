#pragma once

#include "ast.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Interpreter {
public:
  struct Value {
    TypeKind type = TypeKind::Unknown;
    long long intValue = 0;
    bool boolValue = false;
    std::string strValue;

    static Value makeInt(long long v);
    static Value makeBool(bool v);
    static Value makeStr(std::string v);
  };

  class RuntimeEnvironment {
  public:
    RuntimeEnvironment();

    void beginScope();
    void endScope();

    void define(const std::string &name, Value value);
    Value *resolve(const std::string &name);
    const Value *resolve(const std::string &name) const;
    void assign(const std::string &name, Value value);

  private:
    std::vector<std::unordered_map<std::string, Value>> scopes_;
  };

  Interpreter();

  void execute(const std::vector<std::unique_ptr<Stmt>> &program);

private:
  struct ReturnSignal {
    Value value;
  };

  RuntimeEnvironment env_;
  std::unordered_map<std::string, const FuncStmt *> functions_;

  void executeStmt(const Stmt *st);
  Value evalExpr(const Expr *e);
  Value callFunction(const CallExpr *call);

  static std::string valueToString(const Value &v);
  static void requireInts(const Value &lhs, const Value &rhs,
                          const std::string &op);
  static bool equals(const Value &lhs, const Value &rhs);
  static Value defaultValue();
};
