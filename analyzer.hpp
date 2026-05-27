#pragma once

#include "ast.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Analyzer {
public:
  struct Symbol {
    SourceLoc declLoc;
    TypeKind type = TypeKind::Unknown;
    bool used = false;

    bool isArray = false;
    std::size_t arraySize = 0;
    TypeKind elementType = TypeKind::Unknown;
  };

  struct FunctionSymbol {
    SourceLoc declLoc;
    std::size_t arity = 0;
    TypeKind returnType = TypeKind::Unknown;
    bool used = false;
  };

  void analyze(const std::vector<std::unique_ptr<Stmt>> &program);

  int warningCount() const;
  int errorCount() const;
  bool hasErrors() const;

private:
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::unordered_map<std::string, FunctionSymbol> functions_;
  FunctionSymbol *currentFunction_ = nullptr;
  int functionDepth_ = 0;
  int warnings_ = 0;
  int errors_ = 0;

  void beginScope();
  void endScope();

  void reportError(SourceLoc loc, const std::string &msg);

  void declareVar(const std::string &name, SourceLoc loc, TypeKind type);
  void declareArray(const std::string &name, SourceLoc loc,
                    std::size_t arraySize);
  void declareFunction(const FuncStmt *fn);
  Symbol *resolve(const std::string &name);
  TypeKind markUsedAndGetType(const std::string &name, SourceLoc useLoc);
  void assignTo(const std::string &name, SourceLoc loc, TypeKind rhsType);
  TypeKind arrayElementType(const IndexExpr *x);
  TypeKind assignArrayElement(const ArrayAssignExpr *x);

  bool isComparisonOp(const std::string &op) const;
  bool isEqualityOp(const std::string &op) const;

  TypeKind analyzeExpr(const Expr *e);
  void reportBinaryTypeError(const std::string &op, TypeKind lhs, TypeKind rhs);
  void analyzeStmt(const Stmt *st);
};
