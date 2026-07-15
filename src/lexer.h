#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

enum class TT {
    /* --- literals --- */
    INT_LIT, FLOAT_LIT, STR_LIT,

    /* --- keywords --- */
    CLASS, FN, LET, IF, ELSE, ELIF,
    WHILE, FOR, RETURN, EXTENDS, SELF,
    TRUE_KW, FALSE_KW, NULL_KW,
    IMPORT, PUB, IN, BREAK, CONTINUE,
    VIRTUAL, OVERRIDE,
    TRAIT, IMPLEMENTS,

    /* --- builtin type names --- */
    T_INT, T_FLOAT, T_STR, T_BOOL, T_VOID,

    /* --- arithmetic --- */
    PLUS, MINUS, STAR, SLASH, PERCENT,

    /* --- comparison --- */
    EQ, NEQ, LT, GT, LEQ, GEQ,

    /* --- logical --- */
    AND, OR, NOT,

    /* --- assignment --- */
    ASSIGN, PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ,
    INC, DEC,

    /* --- delimiters --- */
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, COLON, DCOLON, DOT, DOTDOT, ARROW, SEMICOLON,

    /* --- special --- */
    IDENT,
    END,
};

struct Token {
    TT          type = TT::END;
    std::string val;
    int         line = 0;
    int         col  = 0;
};

class LexError : public std::runtime_error {
public:
    LexError(const std::string& msg, int line, int col)
        : std::runtime_error(msg), line(line), col(col) {}
    int line, col;
};

class Lexer {
public:
    Lexer(std::string src, std::string fname = "<stdin>");
    std::vector<Token> tokenize();

private:
    std::string src;
    std::string fname;
    size_t pos  = 0;
    int    line = 1;
    int    col  = 1;

    char cur()             const { return pos < src.size() ? src[pos] : '\0'; }
    char peek(int off = 1) const {
        size_t p = pos + (size_t)off;
        return p < src.size() ? src[p] : '\0';
    }
    char adv();
    void skip_ws_comments();
    Token lex_str();
    Token lex_num();
    Token lex_ident();

    static const std::unordered_map<std::string, TT> kw_map;
};
