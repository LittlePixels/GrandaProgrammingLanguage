#pragma once
#include "ast.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>

class TypeError : public std::runtime_error {
public:
    TypeError(const std::string& msg, int line)
        : std::runtime_error(msg), line(line) {}
    int line;
};

/* Lightweight info about a registered class */
struct ClassInfo {
    std::string name;
    std::string base;
    std::unordered_map<std::string, TypeRef> fields; /* includes inherited */
    std::unordered_map<std::string, FnDecl*> methods; /* includes inherited */
};

/* Lightweight info about a registered function */
struct FnInfo {
    TypeRef             return_type;
    std::vector<TypeRef> param_types;
};

class TypeChecker {
public:
    void check(Program& prog);

private:
    std::unordered_map<std::string, ClassInfo> classes;
    std::unordered_map<std::string, FnInfo>    functions;

    /* scope stack — each scope maps name → type */
    std::vector<std::unordered_map<std::string, TypeRef>> scopes;

    TypeRef     current_return_type;
    std::string current_class; /* non-empty while inside a method */

    /* Scope management */
    void push_scope();
    void pop_scope();
    void define(const std::string& name, const TypeRef& type, int line);
    std::optional<TypeRef> lookup(const std::string& name) const;

    /* Registration passes */
    void register_class(ClassDecl& cls);
    void register_fn(FnDecl& fn);

    /* Checking passes */
    void check_class(ClassDecl& cls);
    void check_fn(FnDecl& fn, const std::string& class_ctx = "");
    void check_stmt(Stmt& stmt);
    TypeRef check_expr(Expr& expr);

    /* Helpers */
    const ClassInfo* find_class(const std::string& name, int line) const;
    TypeRef field_type(const std::string& cls, const std::string& field, int line) const;
    FnDecl* find_method(const std::string& cls, const std::string& method, int line) const;
    bool    is_numeric(const TypeRef& t) const;
    bool    compatible(const TypeRef& a, const TypeRef& b) const;
    TypeRef widen(const TypeRef& a, const TypeRef& b, int line) const;

    /* Built-in function type resolution */
    std::optional<TypeRef> check_builtin(const std::string& name,
                                         std::vector<ExprPtr>& args,
                                         int line);
};
