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
    void set_modules(const std::unordered_set<std::string>& mods) { modules = mods; }

private:
    std::ostringstream out;
    int indent = 0;
    int tmp_counter = 0;

    /* known module names */
    std::unordered_set<std::string> modules;

    /* class info: name → ClassDecl* */
    std::unordered_map<std::string, const ClassDecl*> class_decls;

    /* function info: name → FnDecl* (non-methods only) */
    std::unordered_map<std::string, const FnDecl*> fn_decls;

    /* set of classes with GC fields (need trace fn) */
    std::unordered_set<std::string> gc_classes;

    /* vtable root: class name → vtable root class name */
    std::unordered_map<std::string, std::string> vtable_root;

    /* vtable methods: root class → ordered list of virtual method names */
    std::unordered_map<std::string, std::vector<std::string>> vtable_method_list;

    /* trait info: trait name → TraitDecl* */
    std::unordered_map<std::string, const TraitDecl*> trait_decls;

    /* trait method list: trait name → ordered list of method names */
    std::unordered_map<std::string, std::vector<std::string>> trait_method_list;

    /* current function context */
    std::string current_class;
    std::string current_trait; /* non-empty while emitting trait default impl */
    bool in_for_body = false;

    /* maps trait-typed param name → hidden vtable param name (e.g. "d" → "_d_vt") */
    std::unordered_map<std::string, std::string> trait_param_to_vtable;

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

    /* ---- vtable ---- */
    void emit_vtable_struct(const std::string& root_class);
    void emit_vtable_instance(const ClassDecl& cls);
    void emit_vtable_wrapper(const ClassDecl& cls, const std::string& method_name);

    /* ---- trait vtable ---- */
    void emit_trait_vtable_struct(const std::string& trait_name);
    void emit_trait_vtable_wrapper(const std::string& class_name,
                                   const std::string& trait_name,
                                   const std::string& method_name);
    void emit_trait_vtable_instance(const std::string& class_name,
                                   const std::string& trait_name);
    void emit_trait_default_wrapper(const std::string& trait_name,
                                   const std::string& method_name);

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
    std::string get_vtable_root(const std::string& class_name) const;
    bool class_needs_vtable(const std::string& class_name) const;
    const FnDecl* find_method_decl(const std::string& class_name,
                                   const std::string& method_name) const;
    std::string find_defining_class(const std::string& class_name,
                                    const std::string& method_name) const;
    bool is_subclass(const std::string& derived, const std::string& base) const;
    bool class_implements_trait(const std::string& class_name,
                                const std::string& trait_name) const;
};
