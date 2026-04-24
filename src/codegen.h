#pragma once
#include "ast.h"
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>

class CodegenError : public std::runtime_error {
public:
    CodegenError(const std::string& msg, int line)
        : std::runtime_error(msg), line(line) {}
    int line;
};

class Codegen {
public:
    /* Returns the full C source text */
    std::string emit(const Program& prog);

private:
    std::ostringstream out;
    int indent = 0;
    int tmp_counter = 0;

    /* class info: name → ClassDecl* */
    std::unordered_map<std::string, const ClassDecl*> class_decls;

    /* set of classes with GC fields (need trace fn) */
    std::unordered_set<std::string> gc_classes;

    /* current function context */
    std::string current_class;
    bool in_for_body = false;

    /* ---- writers ---- */
    void line_out(const std::string& s);
    void iout(const std::string& s); /* indent + s + newline */
    std::string fresh_tmp();

    /* ---- declarations ---- */
    void emit_class_forward(const ClassDecl& cls);
    void emit_class_struct(const ClassDecl& cls);
    void emit_class_alloc(const ClassDecl& cls);
    void emit_fn_decl(const FnDecl& fn, const std::string& class_ctx = "");
    void emit_fn_body(const FnDecl& fn, const std::string& class_ctx = "");

    /* ---- statements ---- */
    void emit_stmt(const Stmt& stmt);
    void emit_block(const std::vector<StmtPtr>& stmts);

    /* ---- expressions ---- */
    /* Returns a C expression string (may emit temp variable statements first) */
    std::string emit_expr(const Expr& expr);

    /* ---- helpers ---- */
    std::string c_type(const TypeRef& t);
    std::string c_default(const TypeRef& t);
    std::string bin_op_str(BinOp op, const TypeRef& result_type);
    bool class_has_gc_fields(const ClassDecl& cls) const;
    bool type_is_gc(const TypeRef& t) const;
    std::string mangle(const std::string& class_name,
                       const std::string& fn_name) const;
};
