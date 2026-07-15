#include "lexer.h"
#include <cctype>
#include <sstream>

const std::unordered_map<std::string, TT> Lexer::kw_map = {
    {"class",    TT::CLASS},    {"fn",       TT::FN},
    {"let",      TT::LET},      {"if",       TT::IF},
    {"else",     TT::ELSE},     {"elif",     TT::ELIF},
    {"while",    TT::WHILE},    {"for",      TT::FOR},
    {"return",   TT::RETURN},   {"extends",  TT::EXTENDS},
    {"self",     TT::SELF},     {"true",     TT::TRUE_KW},
    {"false",    TT::FALSE_KW}, {"null",     TT::NULL_KW},
    {"import",   TT::IMPORT},   {"pub",      TT::PUB},
    {"in",       TT::IN},       {"break",    TT::BREAK},
    {"continue", TT::CONTINUE},
    {"virtual",  TT::VIRTUAL},   {"override", TT::OVERRIDE},
    {"trait",    TT::TRAIT},     {"implements", TT::IMPLEMENTS},
    {"int",      TT::T_INT},    {"float",    TT::T_FLOAT},
    {"str",      TT::T_STR},    {"bool",     TT::T_BOOL},
    {"void",     TT::T_VOID},
};

Lexer::Lexer(std::string src, std::string fname)
    : src(std::move(src)), fname(std::move(fname)) {}

char Lexer::adv() {
    char c = src[pos++];
    if (c == '\n') { ++line; col = 1; } else { ++col; }
    return c;
}

void Lexer::skip_ws_comments() {
    while (pos < src.size()) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            adv();
        } else if (c == '#') {
            while (pos < src.size() && cur() != '\n') adv();
        } else if (c == '/' && peek() == '*') {
            adv(); adv();
            while (pos < src.size()) {
                if (cur() == '*' && peek() == '/') { adv(); adv(); break; }
                adv();
            }
        } else {
            break;
        }
    }
}

Token Lexer::lex_str() {
    int sl = line, sc = col;
    adv(); /* opening " */
    std::string val;
    while (pos < src.size() && cur() != '"') {
        if (cur() == '\\') {
            adv();
            switch (cur()) {
                case 'n':  val += '\n'; break;
                case 't':  val += '\t'; break;
                case 'r':  val += '\r'; break;
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case '0':  val += '\0'; break;
                default:   val += cur(); break;
            }
            adv();
        } else {
            val += adv();
        }
    }
    if (pos >= src.size())
        throw LexError("Unterminated string literal", sl, sc);
    adv(); /* closing " */
    return {TT::STR_LIT, val, sl, sc};
}

Token Lexer::lex_num() {
    int sl = line, sc = col;
    std::string val;
    bool is_float = false;
    while (pos < src.size() && (std::isdigit((unsigned char)cur()) || cur() == '_'))
        if (cur() != '_') val += adv(); else adv();
    if (cur() == '.' && peek() != '.') {
        is_float = true;
        val += adv();
        while (pos < src.size() && (std::isdigit((unsigned char)cur()) || cur() == '_'))
            if (cur() != '_') val += adv(); else adv();
    }
    if (cur() == 'e' || cur() == 'E') {
        is_float = true;
        val += adv();
        if (cur() == '+' || cur() == '-') val += adv();
        while (pos < src.size() && std::isdigit((unsigned char)cur())) val += adv();
    }
    return {is_float ? TT::FLOAT_LIT : TT::INT_LIT, val, sl, sc};
}

Token Lexer::lex_ident() {
    int sl = line, sc = col;
    std::string val;
    while (pos < src.size() &&
           (std::isalnum((unsigned char)cur()) || cur() == '_'))
        val += adv();
    auto it = kw_map.find(val);
    TT t = (it != kw_map.end()) ? it->second : TT::IDENT;
    return {t, val, sl, sc};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(256);

    while (true) {
        skip_ws_comments();
        if (pos >= src.size()) {
            tokens.push_back({TT::END, "", line, col});
            break;
        }

        int sl = line, sc = col;
        char c = cur();

        if (c == '"') { tokens.push_back(lex_str()); continue; }
        if (std::isdigit((unsigned char)c)) { tokens.push_back(lex_num()); continue; }
        if (std::isalpha((unsigned char)c) || c == '_') { tokens.push_back(lex_ident()); continue; }

        /* multi-char and single-char operators */
        auto emit = [&](TT t, std::string v, int n = 1) {
            Token tok = {t, std::move(v), sl, sc};
            for (int i = 0; i < n; i++) adv();
            tokens.push_back(std::move(tok));
        };

        switch (c) {
            case '+':
                if (peek() == '=') emit(TT::PLUS_EQ, "+=", 2);
                else if (peek() == '+') emit(TT::INC, "++", 2);
                else emit(TT::PLUS, "+");
                break;
            case '-':
                if (peek() == '=') emit(TT::MINUS_EQ, "-=", 2);
                else if (peek() == '-') emit(TT::DEC, "--", 2);
                else if (peek() == '>') emit(TT::ARROW, "->", 2);
                else emit(TT::MINUS, "-");
                break;
            case '*':
                if (peek() == '=') emit(TT::STAR_EQ, "*=", 2);
                else emit(TT::STAR, "*");
                break;
            case '/':
                if (peek() == '=') emit(TT::SLASH_EQ, "/=", 2);
                else emit(TT::SLASH, "/");
                break;
            case '%': emit(TT::PERCENT, "%"); break;
            case '=':
                if (peek() == '=') emit(TT::EQ, "==", 2);
                else emit(TT::ASSIGN, "=");
                break;
            case '!':
                if (peek() == '=') emit(TT::NEQ, "!=", 2);
                else emit(TT::NOT, "!");
                break;
            case '<':
                if (peek() == '=') emit(TT::LEQ, "<=", 2);
                else emit(TT::LT, "<");
                break;
            case '>':
                if (peek() == '=') emit(TT::GEQ, ">=", 2);
                else emit(TT::GT, ">");
                break;
            case '&':
                if (peek() == '&') emit(TT::AND, "&&", 2);
                else throw LexError("Unexpected '&' — did you mean '&&'?", sl, sc);
                break;
            case '|':
                if (peek() == '|') emit(TT::OR, "||", 2);
                else throw LexError("Unexpected '|' — did you mean '||'?", sl, sc);
                break;
            case '(': emit(TT::LPAREN,    "("); break;
            case ')': emit(TT::RPAREN,    ")"); break;
            case '{': emit(TT::LBRACE,    "{"); break;
            case '}': emit(TT::RBRACE,    "}"); break;
            case '[': emit(TT::LBRACKET,  "["); break;
            case ']': emit(TT::RBRACKET,  "]"); break;
            case ',': emit(TT::COMMA,     ","); break;
            case ';': emit(TT::SEMICOLON, ";"); break;
            case ':':
                if (peek() == ':') emit(TT::DCOLON, "::", 2);
                else emit(TT::COLON, ":");
                break;
            case '.':
                if (peek() == '.') emit(TT::DOTDOT, "..", 2);
                else emit(TT::DOT, ".");
                break;
            default: {
                std::string msg = "Unexpected character '";
                msg += c; msg += "'";
                throw LexError(msg, sl, sc);
            }
        }
    }
    return tokens;
}
