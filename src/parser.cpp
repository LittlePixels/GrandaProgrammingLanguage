#include "parser.h"
#include <sstream>

/* -----------------------------------------------------------------------
 * Infrastructure
 * --------------------------------------------------------------------- */
Parser::Parser(std::vector<Token> t) : tokens(std::move(t)) {}

Token& Parser::cur()           { return tokens[pos]; }
Token& Parser::peekTok(int off) {
    size_t p = pos + (size_t)off;
    return p < tokens.size() ? tokens[p] : tokens.back();
}
bool Parser::at(TT t)   const { return tokens[pos].type == t; }
bool Parser::at_any(std::initializer_list<TT> ts) const {
    for (auto t : ts) if (at(t)) return true;
    return false;
}

Token Parser::expect(TT t, const std::string& msg) {
    if (!at(t)) {
        auto& tok = cur();
        std::string m = msg.empty()
            ? "Unexpected token '" + tok.val + "'"
            : msg + " (got '" + tok.val + "')";
        throw ParseError(m, tok.line, tok.col);
    }
    return consume();
}

Token Parser::consume() {
    Token t = tokens[pos];
    if (pos + 1 < tokens.size()) ++pos;
    return t;
}

bool Parser::match(TT t) {
    if (at(t)) { consume(); return true; }
    return false;
}

static ExprPtr mk_expr(Expr::Kind k, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = k; e->line = line;
    return e;
}

/* -----------------------------------------------------------------------
 * Top-level
 * --------------------------------------------------------------------- */
Program Parser::parse() {
    Program prog;
    while (!at(TT::END)) {
        if (at(TT::IMPORT)) {
            prog.imports.push_back(parse_import());
        } else if (at(TT::CLASS) || (at(TT::PUB) && peekTok().type == TT::CLASS)) {
            prog.classes.push_back(parse_class());
        } else if (at(TT::FN) || (at(TT::PUB) && peekTok().type == TT::FN)) {
            prog.functions.push_back(parse_fn());
        } else {
            auto& t = cur();
            throw ParseError(
                "Unexpected '" + t.val + "' at top level", t.line, t.col);
        }
    }
    return prog;
}

ImportDecl Parser::parse_import() {
    int l = cur().line;
    expect(TT::IMPORT, "Expected 'import'");
    std::string path = expect(TT::IDENT, "Expected module name").val;
    while (at(TT::DCOLON)) {
        consume();
        path += "::" + expect(TT::IDENT, "Expected module name after '::'").val;
    }
    return {path, l};
}

ClassDecl Parser::parse_class() {
    int l = cur().line;
    bool pub = match(TT::PUB);
    expect(TT::CLASS, "Expected 'class'");
    std::string name = expect(TT::IDENT, "Expected class name").val;
    std::string base;
    if (match(TT::EXTENDS))
        base = expect(TT::IDENT, "Expected base class name after 'extends'").val;
    expect(TT::LBRACE, "Expected '{' after class name");

    ClassDecl cls;
    cls.is_pub = pub; cls.name = name; cls.base_class = base; cls.line = l;

    while (!at(TT::RBRACE) && !at(TT::END)) {
        if (at(TT::FN) || (at(TT::PUB) && peekTok().type == TT::FN))
            cls.methods.push_back(parse_fn(true));
        else
            cls.fields.push_back(parse_field());
    }
    expect(TT::RBRACE, "Expected '}' to close class body");
    return cls;
}

FieldDecl Parser::parse_field() {
    int l = cur().line;
    std::string name = expect(TT::IDENT, "Expected field name").val;
    expect(TT::COLON, "Expected ':' after field name");
    TypeRef type = parse_type();
    match(TT::SEMICOLON);
    return {name, std::move(type), l};
}

FnDecl Parser::parse_fn(bool is_method) {
    int l = cur().line;
    bool pub = match(TT::PUB);
    expect(TT::FN, "Expected 'fn'");
    std::string name = expect(TT::IDENT, "Expected function name").val;
    expect(TT::LPAREN, "Expected '(' after function name");

    std::vector<Param> params;
    if (!at(TT::RPAREN)) {
        if (at(TT::SELF)) {
            Param p; p.name = "self"; p.type = TypeRef(TypeKind::INFERRED);
            p.line = cur().line; consume(); params.push_back(std::move(p));
            match(TT::COMMA);
        }
        while (!at(TT::RPAREN) && !at(TT::END)) {
            params.push_back(parse_param());
            if (!match(TT::COMMA)) break;
        }
    }
    expect(TT::RPAREN, "Expected ')' to close parameter list");

    TypeRef ret(TypeKind::VOID);
    if (match(TT::ARROW)) ret = parse_type();

    expect(TT::LBRACE, "Expected '{' to open function body");
    auto body = parse_block();
    expect(TT::RBRACE, "Expected '}' to close function body");

    FnDecl fn;
    fn.is_pub = pub; fn.is_method = is_method; fn.name = name;
    fn.params = std::move(params); fn.return_type = std::move(ret);
    fn.body = std::move(body); fn.line = l;
    return fn;
}

Param Parser::parse_param() {
    int l = cur().line;
    std::string name = expect(TT::IDENT, "Expected parameter name").val;
    expect(TT::COLON, "Expected ':' after parameter name");
    TypeRef type = parse_type();
    return {name, std::move(type), l};
}

TypeRef Parser::parse_type() {
    if (at(TT::LBRACKET)) {
        consume();
        TypeRef elem = parse_type();
        expect(TT::RBRACKET, "Expected ']' to close array type");
        return TypeRef::array_of(std::move(elem));
    }
    if (at(TT::T_INT))   { consume(); return TypeRef(TypeKind::INT); }
    if (at(TT::T_FLOAT)) { consume(); return TypeRef(TypeKind::FLOAT); }
    if (at(TT::T_STR))   { consume(); return TypeRef(TypeKind::STR); }
    if (at(TT::T_BOOL))  { consume(); return TypeRef(TypeKind::BOOL); }
    if (at(TT::T_VOID))  { consume(); return TypeRef(TypeKind::VOID); }
    if (at(TT::IDENT)) {
        std::string name = consume().val;
        return TypeRef(std::move(name));
    }
    auto& t = cur();
    throw ParseError("Expected type, got '" + t.val + "'", t.line, t.col);
}

/* -----------------------------------------------------------------------
 * Statements
 * --------------------------------------------------------------------- */
std::vector<StmtPtr> Parser::parse_block() {
    std::vector<StmtPtr> stmts;
    while (!at(TT::RBRACE) && !at(TT::END)) {
        if (match(TT::SEMICOLON)) continue;
        stmts.push_back(parse_stmt());
    }
    return stmts;
}

StmtPtr Parser::parse_stmt() {
    if (at(TT::LET))      return parse_var_decl();
    if (at(TT::IF))       return parse_if();
    if (at(TT::WHILE))    return parse_while();
    if (at(TT::FOR))      return parse_for();
    if (at(TT::RETURN))   return parse_return();
    if (at(TT::BREAK)) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Break; s->line = cur().line;
        consume(); match(TT::SEMICOLON); return s;
    }
    if (at(TT::CONTINUE)) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Continue; s->line = cur().line;
        consume(); match(TT::SEMICOLON); return s;
    }
    int l = cur().line;
    auto e = parse_expr();
    match(TT::SEMICOLON);
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::ExprStmt; s->line = l;
    s->expr = std::move(e);
    return s;
}

StmtPtr Parser::parse_var_decl() {
    int l = cur().line;
    expect(TT::LET, "Expected 'let'");
    std::string name = expect(TT::IDENT, "Expected variable name").val;
    TypeRef type(TypeKind::INFERRED);
    if (match(TT::COLON)) type = parse_type();
    ExprPtr init;
    if (match(TT::ASSIGN)) init = parse_expr();
    match(TT::SEMICOLON);
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::VarDecl; s->line = l;
    s->var_name = name; s->var_type = std::move(type);
    s->var_init = std::move(init);
    return s;
}

StmtPtr Parser::parse_if() {
    int l = cur().line;
    expect(TT::IF, "Expected 'if'");
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::If; s->line = l;

    Stmt::Branch main_br;
    main_br.cond = parse_expr();
    expect(TT::LBRACE, "Expected '{' after if condition");
    main_br.body = parse_block();
    expect(TT::RBRACE, "Expected '}' to close if body");
    s->if_branches.push_back(std::move(main_br));

    while (at(TT::ELIF)) {
        consume();
        Stmt::Branch br;
        br.cond = parse_expr();
        expect(TT::LBRACE, "Expected '{' after elif condition");
        br.body = parse_block();
        expect(TT::RBRACE, "Expected '}' to close elif body");
        s->if_branches.push_back(std::move(br));
    }
    if (match(TT::ELSE)) {
        expect(TT::LBRACE, "Expected '{' after else");
        s->else_body = parse_block();
        expect(TT::RBRACE, "Expected '}' to close else body");
    }
    return s;
}

StmtPtr Parser::parse_while() {
    int l = cur().line;
    expect(TT::WHILE, "Expected 'while'");
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::While; s->line = l;
    s->while_cond = parse_expr();
    expect(TT::LBRACE, "Expected '{' after while condition");
    s->body = parse_block();
    expect(TT::RBRACE, "Expected '}' to close while body");
    return s;
}

StmtPtr Parser::parse_for() {
    int l = cur().line;
    expect(TT::FOR, "Expected 'for'");
    std::string var = expect(TT::IDENT, "Expected loop variable").val;
    expect(TT::IN, "Expected 'in' after loop variable");
    auto iter = parse_expr();
    expect(TT::LBRACE, "Expected '{' after for iterable");
    auto body = parse_block();
    expect(TT::RBRACE, "Expected '}' to close for body");
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::For; s->line = l;
    s->for_var = var; s->for_iter = std::move(iter);
    s->body = std::move(body);
    return s;
}

StmtPtr Parser::parse_return() {
    int l = cur().line;
    expect(TT::RETURN, "Expected 'return'");
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Return; s->line = l;
    if (!at(TT::RBRACE) && !at(TT::SEMICOLON) && !at(TT::END))
        s->ret_val = parse_expr();
    match(TT::SEMICOLON);
    return s;
}

/* -----------------------------------------------------------------------
 * Expressions
 * --------------------------------------------------------------------- */
ExprPtr Parser::parse_expr()   { return parse_assign(); }

ExprPtr Parser::parse_assign() {
    auto left = parse_or();
    int l = cur().line;
    if (at(TT::ASSIGN)) {
        consume();
        auto e = mk_expr(Expr::Kind::Assign, l);
        e->left = std::move(left);
        e->right = parse_assign();
        return e;
    }
    auto compound = [&](BinOp op) -> ExprPtr {
        consume();
        auto e = mk_expr(Expr::Kind::CompAssign, l);
        e->comp_op = op;
        e->left  = std::move(left);
        e->right = parse_assign();
        return e;
    };
    if (at(TT::PLUS_EQ))  return compound(BinOp::ADD);
    if (at(TT::MINUS_EQ)) return compound(BinOp::SUB);
    if (at(TT::STAR_EQ))  return compound(BinOp::MUL);
    if (at(TT::SLASH_EQ)) return compound(BinOp::DIV);
    return left;
}

ExprPtr Parser::parse_or() {
    auto left = parse_and();
    while (at(TT::OR)) {
        int l = cur().line; consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = BinOp::OR;
        e->left = std::move(left); e->right = parse_and();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_equality();
    while (at(TT::AND)) {
        int l = cur().line; consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = BinOp::AND;
        e->left = std::move(left); e->right = parse_equality();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_equality() {
    auto left = parse_comparison();
    while (at_any({TT::EQ, TT::NEQ})) {
        int l = cur().line;
        BinOp op = at(TT::EQ) ? BinOp::EQ : BinOp::NEQ;
        consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = op;
        e->left = std::move(left); e->right = parse_comparison();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_addition();
    /* range operator has lowest precedence within this level */
    if (at(TT::DOTDOT)) {
        int l = cur().line; consume();
        auto e = mk_expr(Expr::Kind::Range, l);
        e->left = std::move(left); e->right = parse_addition();
        return e;
    }
    while (at_any({TT::LT, TT::GT, TT::LEQ, TT::GEQ})) {
        int l = cur().line;
        BinOp op;
        switch (cur().type) {
            case TT::LT:  op = BinOp::LT;  break;
            case TT::GT:  op = BinOp::GT;  break;
            case TT::LEQ: op = BinOp::LEQ; break;
            default:      op = BinOp::GEQ; break;
        }
        consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = op;
        e->left = std::move(left); e->right = parse_addition();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_addition() {
    auto left = parse_multiplication();
    while (at_any({TT::PLUS, TT::MINUS})) {
        int l = cur().line;
        BinOp op = at(TT::PLUS) ? BinOp::ADD : BinOp::SUB;
        consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = op;
        e->left = std::move(left); e->right = parse_multiplication();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_multiplication() {
    auto left = parse_unary();
    while (at_any({TT::STAR, TT::SLASH, TT::PERCENT})) {
        int l = cur().line;
        BinOp op;
        switch (cur().type) {
            case TT::STAR:    op = BinOp::MUL; break;
            case TT::SLASH:   op = BinOp::DIV; break;
            default:          op = BinOp::MOD; break;
        }
        consume();
        auto e = mk_expr(Expr::Kind::Binary, l);
        e->bin_op = op;
        e->left = std::move(left); e->right = parse_unary();
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    int l = cur().line;
    if (at(TT::NOT)) {
        consume();
        auto e = mk_expr(Expr::Kind::Unary, l);
        e->un_op = UnOp::NOT; e->operand = parse_unary();
        return e;
    }
    if (at(TT::MINUS)) {
        consume();
        auto e = mk_expr(Expr::Kind::Unary, l);
        e->un_op = UnOp::NEG; e->operand = parse_unary();
        return e;
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    auto expr = parse_primary();
    while (true) {
        int l = cur().line;
        if (at(TT::DOT)) {
            consume();
            std::string member = expect(TT::IDENT, "Expected member name after '.'").val;
            if (at(TT::LPAREN)) {
                consume();
                auto e = mk_expr(Expr::Kind::MethodCall, l);
                e->object = std::move(expr); e->method_name = member;
                while (!at(TT::RPAREN) && !at(TT::END)) {
                    e->args.push_back(parse_expr());
                    if (!match(TT::COMMA)) break;
                }
                expect(TT::RPAREN, "Expected ')' after method arguments");
                expr = std::move(e);
            } else {
                auto e = mk_expr(Expr::Kind::FieldAccess, l);
                e->object = std::move(expr); e->str_val = member;
                expr = std::move(e);
            }
        } else if (at(TT::LBRACKET)) {
            consume();
            auto idx = parse_expr();
            expect(TT::RBRACKET, "Expected ']' after index");
            auto e = mk_expr(Expr::Kind::Index, l);
            e->left = std::move(expr); e->index = std::move(idx);
            expr = std::move(e);
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::parse_primary() {
    int l = cur().line;

    if (at(TT::INT_LIT)) {
        auto tok = consume();
        auto e = mk_expr(Expr::Kind::IntLit, l);
        e->int_val = std::stoll(tok.val); e->str_val = tok.val;
        return e;
    }
    if (at(TT::FLOAT_LIT)) {
        auto tok = consume();
        auto e = mk_expr(Expr::Kind::FloatLit, l);
        e->float_val = std::stod(tok.val); e->str_val = tok.val;
        return e;
    }
    if (at(TT::STR_LIT)) {
        auto tok = consume();
        auto e = mk_expr(Expr::Kind::StrLit, l);
        e->str_val = tok.val; return e;
    }
    if (at(TT::TRUE_KW)) {
        consume();
        auto e = mk_expr(Expr::Kind::BoolLit, l);
        e->bool_val = true; return e;
    }
    if (at(TT::FALSE_KW)) {
        consume();
        auto e = mk_expr(Expr::Kind::BoolLit, l);
        e->bool_val = false; return e;
    }
    if (at(TT::NULL_KW))   { consume(); return mk_expr(Expr::Kind::NullLit, l); }
    if (at(TT::SELF))      { consume(); return mk_expr(Expr::Kind::SelfExpr, l); }

    if (at(TT::LBRACKET)) {
        consume();
        auto e = mk_expr(Expr::Kind::ArrayLit, l);
        while (!at(TT::RBRACKET) && !at(TT::END)) {
            e->elements.push_back(parse_expr());
            if (!match(TT::COMMA)) break;
        }
        expect(TT::RBRACKET, "Expected ']' to close array literal");
        return e;
    }

    if (at(TT::LPAREN)) {
        consume();
        auto e = parse_expr();
        expect(TT::RPAREN, "Expected ')' to close grouped expression");
        return e;
    }

    if (at(TT::IDENT)) {
        std::string name = consume().val;

        /* ClassName::method(args) */
        if (at(TT::DCOLON)) {
            consume();
            std::string method = expect(TT::IDENT, "Expected method name after '::'").val;
            expect(TT::LPAREN, "Expected '(' after static method name");
            auto e = mk_expr(Expr::Kind::StaticCall, l);
            e->class_name = name; e->method_name = method;
            while (!at(TT::RPAREN) && !at(TT::END)) {
                e->args.push_back(parse_expr());
                if (!match(TT::COMMA)) break;
            }
            expect(TT::RPAREN, "Expected ')' after static call arguments");
            return e;
        }

        /* ClassName { field: val, ... }  or  ClassName {} */
        if (at(TT::LBRACE)) {
            bool is_cons = false;
            if (pos + 1 < tokens.size() && tokens[pos + 1].type == TT::IDENT &&
                pos + 2 < tokens.size() && tokens[pos + 2].type == TT::COLON)
                is_cons = true;
            else if (pos + 1 < tokens.size() && tokens[pos + 1].type == TT::RBRACE)
                is_cons = true;
            if (is_cons) {
                consume(); /* { */
                auto e = mk_expr(Expr::Kind::ClassCons, l);
                e->class_name = name;
                while (!at(TT::RBRACE) && !at(TT::END)) {
                    std::string fname = expect(TT::IDENT, "Expected field name").val;
                    expect(TT::COLON, "Expected ':' after field name in constructor");
                    auto val = parse_expr();
                    e->field_inits.push_back({fname, std::move(val)});
                    if (!match(TT::COMMA)) break;
                }
                expect(TT::RBRACE, "Expected '}' to close class constructor");
                return e;
            }
        }

        /* fn(args) */
        if (at(TT::LPAREN)) {
            consume();
            auto e = mk_expr(Expr::Kind::Call, l);
            e->callee_name = name;
            while (!at(TT::RPAREN) && !at(TT::END)) {
                e->args.push_back(parse_expr());
                if (!match(TT::COMMA)) break;
            }
            expect(TT::RPAREN, "Expected ')' after call arguments");
            return e;
        }

        /* plain identifier */
        auto e = mk_expr(Expr::Kind::Ident, l);
        e->str_val = name;
        return e;
    }

    auto& t = cur();
    throw ParseError("Unexpected token '" + t.val + "' in expression", t.line, t.col);
}
