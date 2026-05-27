#include "analyzer.hpp"
#include "ast.hpp"
#include "interpreter.hpp"
#include "lexer.hpp"
#include "optimizer.hpp"
#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string readFileOrDie(const std::string &path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open file: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <program_file>\n";
    return 2;
  }

  try {
    // 1) read source
    std::string src = readFileOrDie(argv[1]);

    // 2) lex
    Lexer lx(std::move(src));
    auto toks = lx.tokenize();

    // 3) print tokens
    std::cout << "=== TOKENS ===\n";
    for (const auto &t : toks) {
      std::cout << t.line << ":" << t.col << "  " << kindName(t.kind) << "  '"
                << t.lexeme << "'\n";
    }

    // 4) parse + print AST
    std::cout << "\n=== AST ===\n";
    Parser ps(std::move(toks));
    auto prog = ps.parseProgram();
    for (auto &st : prog) {
      st->dump(std::cout, 0);
    }

    // 5) semantic analysis
    std::cout << "\n=== ANALYSIS ===\n";
    Analyzer az;
    az.analyze(prog);

    if (az.hasErrors()) {
      std::cerr << "Execution aborted due to semantic errors\n";
      return 1;
    }

    // 6) compile-time optimization: constant folding
    Optimizer opt;
    opt.optimize(prog);

    std::cout << "\n=== OPTIMIZED AST ===\n";
    for (auto &st : prog) {
      st->dump(std::cout, 0);
    }

    std::cout << "\n=== EXECUTION ===\n";
    Interpreter it;
    it.execute(prog);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
