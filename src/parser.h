#pragma once
#include "lexer.h"
#include "ast.h"
#include <stdexcept>

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, int line, int col)
        : std::runtime_error(msg), line(line), col(col) {}
    int line, col;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    Program parse();

private:
    std::vector<Token> tokens;
    size_t pos = 0;

    Token& cur();
    Token& peekTok(int off = 1);
    bool   at(TT t)                                  const;
    bool   at_any(std::initializer_list<TT> ts)      const;
    Token  expect(TT t, const std::string& msg = "");
    Token  consume();
    bool   match(TT t);

    /* top-level */
    ImportDecl parse_import();
    ClassDecl  parse_class();
    FnDecl     parse_fn(bool is_method = false);
    Param      parse_param();
    TypeRef    parse_type();
    FieldDecl  parse_field();

    /* statements */
    std::vector<StmtPtr> parse_block();
    StmtPtr parse_stmt();
    StmtPtr parse_var_decl();
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_for();
    StmtPtr parse_return();

    /* expressions — precedence levels (low → high) */
    ExprPtr parse_expr();
    ExprPtr parse_assign();
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_equality();
    ExprPtr parse_comparison(); /* also handles range ".." */
    ExprPtr parse_addition();
    ExprPtr parse_multiplication();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();
};
