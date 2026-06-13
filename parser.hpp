#pragma once

#include "ast.hpp"
#include "lexer.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class Parser {

public:
  explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

  std::vector<std::unique_ptr<Stmt>> parseProgram() {
    std::vector<std::unique_ptr<Stmt>> out;
    while (!atEnd()) {
      out.push_back(parseDeclaration());
    }
    return out;
  }

private:
  std::vector<Token> toks_;
  std::size_t i_ = 0;

  const Token &cur() const { return toks_[i_]; }
  const Token &prev() const { return toks_[i_ - 1]; }
  bool atEnd() const { return cur().kind == TokenKind::End; }

  const Token &advance() {
    if (!atEnd())
      i_++;
    return prev();
  }

  [[noreturn]] void errorHere(const std::string &msg) const {
    throw std::runtime_error(msg + " at " + std::to_string(cur().line) + ":" +
                             std::to_string(cur().col));
  }

  bool checkSym(const char *s) const {
    return cur().kind == TokenKind::Symbol && cur().lexeme == s;
  }
  bool matchSym(const char *s) {
    if (checkSym(s)) {
      advance();
      return true;
    }
    return false;
  }

  bool checkKw(const char *s) const {
    return cur().kind == TokenKind::Keyword && cur().lexeme == s;
  }
  bool matchKw(const char *s) {
    if (checkKw(s)) {
      advance();
      return true;
    }
    return false;
  }

  void consumeSym(const char *s, const std::string &msg) {
    if (!matchSym(s))
      errorHere(msg);
  }

  Token consumeIdent(const std::string &msg) {
    if (cur().kind != TokenKind::Identifier)
      errorHere(msg);
    return advance();
  }

  // -------- declarations / statements --------
  std::unique_ptr<Stmt> parseDeclaration() {
    if (matchKw("fun"))
      return parseFuncDecl();
    if (matchKw("var"))
      return parseVarDecl();
    return parseStatement();
  }

  std::unique_ptr<Stmt> parseFuncDecl() {
    Token name = consumeIdent("expected function name");
    consumeSym("(", "expected '(' after function name");

    std::vector<std::string> params;
    if (!checkSym(")")) {
      do {
        Token param = consumeIdent("expected parameter name");
        params.push_back(param.lexeme);
      } while (matchSym(","));
    }

    consumeSym(")", "expected ')' after function parameters");
    consumeSym("{", "expected '{' before function body");
    auto body = parseBlock();

    return std::make_unique<FuncStmt>(name.lexeme,
                                      SourceLoc{name.line, name.col},
                                      std::move(params), std::move(body));
  }

  std::unique_ptr<Stmt> parseVarDecl() {
    Token name = consumeIdent("expected variable name");
    std::size_t arraySize = 0;

    if (matchSym("[")) {
      if (cur().kind != TokenKind::Number) {
        errorHere("expected array size");
      }

      Token sizeTok = advance();
      long long parsedSize = std::stoll(sizeTok.lexeme);

      if (parsedSize <= 0) {
        throw std::runtime_error("array size must be positive at " +
                                 std::to_string(sizeTok.line) + ":" +
                                 std::to_string(sizeTok.col));
      }

      arraySize = static_cast<std::size_t>(parsedSize);
      consumeSym("]", "expected ']' after array size");
    }

    std::unique_ptr<Expr> init;
    if (matchSym("=")) {
      if (arraySize > 0) {
        errorHere("array declaration cannot have initializer");
      }

      init = parseExpression();
    }

    consumeSym(";", "expected ';' after variable declaration");

    return std::make_unique<VarStmt>(name.lexeme,
                                     SourceLoc{name.line, name.col},
                                     std::move(init), arraySize);
  }

  std::unique_ptr<Stmt> parseStatement() {
    if (matchKw("return"))
      return parseReturn();
    if (matchKw("while"))
      return parseWhile();
    if (matchKw("if"))
      return parseIf();
    if (matchKw("print"))
      return parsePrint();
    if (matchSym("{"))
      return parseBlock();
    return parseExprStmt();
  }

  std::unique_ptr<Stmt> parseReturn() {
    Token ret = prev();
    std::unique_ptr<Expr> value;
    if (!checkSym(";"))
      value = parseExpression();
    consumeSym(";", "expected ';' after return value");
    return std::make_unique<ReturnStmt>(SourceLoc{ret.line, ret.col},
                                        std::move(value));
  }

  std::unique_ptr<Stmt> parseWhile() {
    consumeSym("(", "expected '(' after 'while'");
    auto cond = parseExpression();
    consumeSym(")", "expected ')' after while condition");
    auto body = parseStatement();
    return std::make_unique<WhileStmt>(std::move(cond), std::move(body));
  }

  std::unique_ptr<Stmt> parseIf() {
    consumeSym("(", "expected '(' after 'if'");
    auto cond = parseExpression();
    consumeSym(")", "expected ')' after if condition");
    auto thenBranch = parseStatement();
    std::unique_ptr<Stmt> elseBranch;
    if (matchKw("else"))
      elseBranch = parseStatement();
    return std::make_unique<IfStmt>(std::move(cond), std::move(thenBranch),
                                    std::move(elseBranch));
  }

  std::unique_ptr<Stmt> parsePrint() {
    auto e = parseExpression();
    consumeSym(";", "expected ';' after print expression");
    return std::make_unique<PrintStmt>(std::move(e));
  }

  std::unique_ptr<Stmt> parseBlock() {
    std::vector<std::unique_ptr<Stmt>> stmts;
    while (!atEnd() && !checkSym("}")) {
      stmts.push_back(parseDeclaration());
    }
    consumeSym("}", "expected '}' after block");
    return std::make_unique<BlockStmt>(std::move(stmts));
  }

  std::unique_ptr<Stmt> parseExprStmt() {
    auto e = parseExpression();
    consumeSym(";", "expected ';' after expression");
    return std::make_unique<ExprStmt>(std::move(e));
  }

  // -------- expressions (precedence) --------
  std::unique_ptr<Expr> parseExpression() { return parseAssignment(); }

  std::unique_ptr<Expr> parseAssignment() {
    auto lhs = parseOr();

    if (matchSym("=")) {
      auto value = parseAssignment();

      // assignment target must be identifier
      if (auto *id = dynamic_cast<IdentExpr *>(lhs.get())) {
        std::string name = id->name;
        SourceLoc loc = id->loc;
        return std::make_unique<AssignExpr>(std::move(name), loc,
                                            std::move(value));
      }

      // or indexed array element: a[i] = value
      if (auto *idx = dynamic_cast<IndexExpr *>(lhs.get())) {
        std::string name = idx->name;
        SourceLoc loc = idx->loc;
        auto index = std::move(idx->index);

        return std::make_unique<ArrayAssignExpr>(
            std::move(name), loc, std::move(index), std::move(value));
      }

      errorHere("invalid assignment target");
    }

    return lhs;
  }

  std::unique_ptr<Expr> parseOr() {
    auto e = parseAnd();
    while (checkSym("||")) {
      std::string op = advance().lexeme;
      auto r = parseAnd();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseAnd() {
    auto e = parseEquality();
    while (checkSym("&&")) {
      std::string op = advance().lexeme;
      auto r = parseEquality();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseEquality() {
    auto e = parseComparison();
    while (checkSym("==") || checkSym("!=")) {
      std::string op = advance().lexeme;
      auto r = parseComparison();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseComparison() {
    auto e = parseTerm();
    while (checkSym("<") || checkSym("<=") || checkSym(">") || checkSym(">=")) {
      std::string op = advance().lexeme;
      auto r = parseTerm();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseTerm() {
    auto e = parseFactor();
    while (checkSym("+") || checkSym("-")) {
      std::string op = advance().lexeme;
      auto r = parseFactor();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseFactor() {
    auto e = parseUnary();
    while (checkSym("*") || checkSym("/")) {
      std::string op = advance().lexeme;
      auto r = parseUnary();
      e = std::make_unique<BinaryExpr>(std::move(op), std::move(e),
                                       std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> parseUnary() {
    if (checkSym("-") || checkSym("!")) {
      std::string op = advance().lexeme;
      auto rhs = parseUnary();
      return std::make_unique<UnaryExpr>(std::move(op), std::move(rhs));
    }
    return parseCall();
  }

  std::unique_ptr<Expr> parseCall() {
    auto e = parsePrimary();

    while (true) {
      if (matchSym("(")) {
        auto *id = dynamic_cast<IdentExpr *>(e.get());
        if (!id)
          errorHere("only named functions can be called");

        std::vector<std::unique_ptr<Expr>> args;
        if (!checkSym(")")) {
          do {
            args.push_back(parseExpression());
          } while (matchSym(","));
        }
        consumeSym(")", "expected ')' after function arguments");

        std::string callee = id->name;
        SourceLoc loc = id->loc;
        e = std::make_unique<CallExpr>(std::move(callee), loc, std::move(args));
        continue;
      }

      if (matchSym("[")) {
        auto *id = dynamic_cast<IdentExpr *>(e.get());
        if (!id)
          errorHere("only named arrays can be indexed");

        auto index = parseExpression();
        consumeSym("]", "expected ']' after array index");

        std::string name = id->name;
        SourceLoc loc = id->loc;
        e = std::make_unique<IndexExpr>(std::move(name), loc, std::move(index));
        continue;
      }

      break;
    }

    return e;
  }
  std::unique_ptr<Expr> parsePrimary() {
    if (cur().kind == TokenKind::Number) {
      auto s = advance().lexeme;
      return std::make_unique<NumberExpr>(std::stoll(s));
    }

    if (cur().kind == TokenKind::String) {
      std::string s = advance().lexeme;
      return std::make_unique<StringExpr>(std::move(s));
    }

    if (checkKw("true")) {
      advance();
      return std::make_unique<BoolExpr>(true);
    }

    if (checkKw("false")) {
      advance();
      return std::make_unique<BoolExpr>(false);
    }

    if (cur().kind == TokenKind::Identifier) {
      Token tok = advance();
      return std::make_unique<IdentExpr>(tok.lexeme,
                                         SourceLoc{tok.line, tok.col});
    }

    if (matchSym("(")) {
      auto e = parseExpression();
      consumeSym(")", "expected ')' after expression");
      return e;
    }

    errorHere("expected expression");
  }
};
