#include "type_checker.h"
#include <sstream>

/* -----------------------------------------------------------------------
 * Scope helpers
 * --------------------------------------------------------------------- */
void TypeChecker::push_scope() { scopes.emplace_back(); }
void TypeChecker::pop_scope()  { scopes.pop_back(); }

void TypeChecker::define(const std::string& name, const TypeRef& type, int line) {
    if (scopes.empty()) throw TypeError("No active scope", line);
    scopes.back()[name] = type.clone();
}

std::optional<TypeRef> TypeChecker::lookup(const std::string& name) const {
    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) return it->second.clone();
    }
    return std::nullopt;
}

/* -----------------------------------------------------------------------
 * Class helpers
 * --------------------------------------------------------------------- */
const ClassInfo* TypeChecker::find_class(const std::string& name, int line) const {
    auto it = classes.find(name);
    if (it == classes.end())
        throw TypeError("Unknown class '" + name + "'", line);
    return &it->second;
}

TypeRef TypeChecker::field_type(const std::string& cls,
                                const std::string& field, int line) const {
    const ClassInfo* info = find_class(cls, line);
    auto it = info->fields.find(field);
    if (it == info->fields.end())
        throw TypeError("Class '" + cls + "' has no field '" + field + "'", line);
    return it->second.clone();
}

FnDecl* TypeChecker::find_method(const std::string& cls,
                                 const std::string& method, int line) const {
    const ClassInfo* info = find_class(cls, line);
    auto it = info->methods.find(method);
    if (it == info->methods.end())
        throw TypeError("Class '" + cls + "' has no method '" + method + "'", line);
    return it->second;
}

bool TypeChecker::is_numeric(const TypeRef& t) const {
    return t.kind == TypeKind::INT || t.kind == TypeKind::FLOAT;
}

bool TypeChecker::compatible(const TypeRef& a, const TypeRef& b) const {
    if (a == b) return true;
    /* int is implicitly coercible to float */
    if (a.kind == TypeKind::INT && b.kind == TypeKind::FLOAT) return true;
    if (a.kind == TypeKind::FLOAT && b.kind == TypeKind::INT) return true;
    /* null is compatible with any GC type */
    if (a.kind == TypeKind::INFERRED) return true; /* null literal */
    if (b.kind == TypeKind::INFERRED) return true;
    return false;
}

TypeRef TypeChecker::widen(const TypeRef& a, const TypeRef& b, int line) const {
    if (a == b) return a.clone();
    if (a.kind == TypeKind::INT   && b.kind == TypeKind::FLOAT) return TypeRef(TypeKind::FLOAT);
    if (a.kind == TypeKind::FLOAT && b.kind == TypeKind::INT)   return TypeRef(TypeKind::FLOAT);
    throw TypeError("Incompatible types: " + a.str() + " and " + b.str(), line);
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */
void TypeChecker::register_class(ClassDecl& cls) {
    ClassInfo info;
    info.name = cls.name;
    info.base = cls.base_class;

    /* inherit base fields/methods first */
    if (!cls.base_class.empty()) {
        auto it = classes.find(cls.base_class);
        if (it == classes.end())
            throw TypeError("Base class '" + cls.base_class + "' not defined "
                            "(define it before '" + cls.name + "')", cls.line);
        info.fields  = it->second.fields;
        info.methods = it->second.methods;
    }

    for (auto& f : cls.fields)
        info.fields[f.name] = f.type.clone();

    for (auto& m : cls.methods)
        info.methods[m.name] = &m;

    classes[cls.name] = std::move(info);
}

void TypeChecker::register_fn(FnDecl& fn) {
    FnInfo info;
    info.return_type = fn.return_type.clone();
    for (auto& p : fn.params)
        info.param_types.push_back(p.type.clone());
    functions[fn.name] = std::move(info);
}

/* -----------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */
void TypeChecker::check(Program& prog) {
    /* Pass 1: register all classes (order matters for inheritance) */
    for (auto& cls : prog.classes) register_class(cls);
    /* Pass 2: register all top-level functions */
    for (auto& fn  : prog.functions) register_fn(fn);

    /* Pass 3: check each class body */
    for (auto& cls : prog.classes) check_class(cls);
    /* Pass 4: check each top-level function */
    for (auto& fn  : prog.functions) check_fn(fn);
}

void TypeChecker::check_class(ClassDecl& cls) {
    for (auto& m : cls.methods)
        check_fn(m, cls.name);
}

void TypeChecker::check_fn(FnDecl& fn, const std::string& class_ctx) {
    current_class       = class_ctx;
    current_return_type = fn.return_type.clone();

    push_scope();
    /* bind 'self' if it's a method */
    if (!class_ctx.empty())
        define("self", TypeRef(class_ctx), fn.line);

    for (auto& p : fn.params) {
        if (p.name == "self") continue; /* already bound */
        define(p.name, p.type, p.line);
    }

    for (auto& stmt : fn.body)
        check_stmt(*stmt);

    pop_scope();
    current_class = "";
}

/* -----------------------------------------------------------------------
 * Statement checking
 * --------------------------------------------------------------------- */
void TypeChecker::check_stmt(Stmt& stmt) {
    switch (stmt.kind) {

    case Stmt::Kind::VarDecl: {
        TypeRef declared = stmt.var_type.clone();
        if (stmt.var_init) {
            TypeRef init_type = check_expr(*stmt.var_init);
            if (declared.kind == TypeKind::INFERRED) {
                declared = init_type.clone();
            } else if (!compatible(init_type, declared)) {
                throw TypeError(
                    "Cannot assign " + init_type.str() +
                    " to variable of type " + declared.str(), stmt.line);
            }
        }
        if (declared.kind == TypeKind::INFERRED)
            throw TypeError("Cannot infer type of '" + stmt.var_name +
                            "' — provide an initializer or explicit type", stmt.line);
        stmt.var_type = declared.clone();
        define(stmt.var_name, declared, stmt.line);
        break;
    }

    case Stmt::Kind::ExprStmt:
        check_expr(*stmt.expr);
        break;

    case Stmt::Kind::If:
        for (auto& br : stmt.if_branches) {
            TypeRef ct = check_expr(*br.cond);
            if (ct.kind != TypeKind::BOOL)
                throw TypeError("If condition must be bool, got " + ct.str(),
                                stmt.line);
            push_scope();
            for (auto& s : br.body) check_stmt(*s);
            pop_scope();
        }
        if (!stmt.else_body.empty()) {
            push_scope();
            for (auto& s : stmt.else_body) check_stmt(*s);
            pop_scope();
        }
        break;

    case Stmt::Kind::While: {
        TypeRef ct = check_expr(*stmt.while_cond);
        if (ct.kind != TypeKind::BOOL)
            throw TypeError("While condition must be bool, got " + ct.str(),
                            stmt.line);
        push_scope();
        for (auto& s : stmt.body) check_stmt(*s);
        pop_scope();
        break;
    }

    case Stmt::Kind::For: {
        TypeRef iter_type = check_expr(*stmt.for_iter);
        TypeRef elem_type(TypeKind::INT); /* default for range */
        if (iter_type.kind == TypeKind::ARRAY) {
            if (!iter_type.elem)
                throw TypeError("Array has unknown element type", stmt.line);
            elem_type = iter_type.elem->clone();
        } else if (iter_type.kind == TypeKind::CLASS &&
                   iter_type.class_name == "__Range") {
            elem_type = TypeRef(TypeKind::INT);
        } else if (iter_type.kind != TypeKind::INT) {
            /* allow plain int range variable e.g. for i in 0..n */
        }
        push_scope();
        define(stmt.for_var, elem_type, stmt.line);
        for (auto& s : stmt.body) check_stmt(*s);
        pop_scope();
        break;
    }

    case Stmt::Kind::Return: {
        TypeRef ret(TypeKind::VOID);
        if (stmt.ret_val) ret = check_expr(*stmt.ret_val);
        if (!compatible(ret, current_return_type))
            throw TypeError(
                "Return type mismatch: expected " + current_return_type.str() +
                ", got " + ret.str(), stmt.line);
        break;
    }

    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Expression checking
 * --------------------------------------------------------------------- */
std::optional<TypeRef> TypeChecker::check_builtin(
        const std::string& name, std::vector<ExprPtr>& args, int line) {

    if (name == "print" || name == "println") {
        if (args.size() != 1)
            throw TypeError(name + "() takes exactly 1 argument", line);
        check_expr(*args[0]); /* accept any type */
        return TypeRef(TypeKind::VOID);
    }
    if (name == "len") {
        if (args.size() != 1)
            throw TypeError("len() takes exactly 1 argument", line);
        TypeRef t = check_expr(*args[0]);
        if (t.kind != TypeKind::ARRAY && t.kind != TypeKind::STR)
            throw TypeError("len() requires str or array, got " + t.str(), line);
        return TypeRef(TypeKind::INT);
    }
    if (name == "str") {
        if (args.size() != 1)
            throw TypeError("str() takes exactly 1 argument", line);
        check_expr(*args[0]);
        return TypeRef(TypeKind::STR);
    }
    if (name == "int") {
        if (args.size() != 1)
            throw TypeError("int() takes exactly 1 argument", line);
        TypeRef t = check_expr(*args[0]);
        (void)t;
        return TypeRef(TypeKind::INT);
    }
    if (name == "float") {
        if (args.size() != 1)
            throw TypeError("float() takes exactly 1 argument", line);
        check_expr(*args[0]);
        return TypeRef(TypeKind::FLOAT);
    }
    if (name == "assert") {
        if (args.size() < 1)
            throw TypeError("assert() takes at least 1 argument", line);
        check_expr(*args[0]);
        return TypeRef(TypeKind::VOID);
    }
    if (name == "push") {
        if (args.size() != 2)
            throw TypeError("push() takes exactly 2 arguments", line);
        check_expr(*args[0]); check_expr(*args[1]);
        return TypeRef(TypeKind::VOID);
    }
    if (name == "pop") {
        if (args.size() != 1)
            throw TypeError("pop() takes exactly 1 argument", line);
        TypeRef t = check_expr(*args[0]);
        if (t.kind == TypeKind::ARRAY && t.elem)
            return t.elem->clone();
        return TypeRef(TypeKind::INFERRED);
    }
    return std::nullopt;
}

TypeRef TypeChecker::check_expr(Expr& expr) {
    switch (expr.kind) {

    case Expr::Kind::IntLit:
        expr.resolved_type = TypeRef(TypeKind::INT); break;
    case Expr::Kind::FloatLit:
        expr.resolved_type = TypeRef(TypeKind::FLOAT); break;
    case Expr::Kind::StrLit:
        expr.resolved_type = TypeRef(TypeKind::STR); break;
    case Expr::Kind::BoolLit:
        expr.resolved_type = TypeRef(TypeKind::BOOL); break;
    case Expr::Kind::NullLit:
        expr.resolved_type = TypeRef(TypeKind::INFERRED); break;

    case Expr::Kind::Ident: {
        auto t = lookup(expr.str_val);
        if (!t) throw TypeError("Undefined variable '" + expr.str_val + "'", expr.line);
        expr.resolved_type = t->clone(); break;
    }

    case Expr::Kind::SelfExpr: {
        if (current_class.empty())
            throw TypeError("'self' used outside a method", expr.line);
        expr.resolved_type = TypeRef(current_class); break;
    }

    case Expr::Kind::Binary: {
        TypeRef lt = check_expr(*expr.left);
        TypeRef rt = check_expr(*expr.right);
        switch (expr.bin_op) {
            case BinOp::ADD:
                if (lt.kind == TypeKind::STR && rt.kind == TypeKind::STR) {
                    expr.resolved_type = TypeRef(TypeKind::STR); break;
                }
                /* fall-through */
            case BinOp::SUB: case BinOp::MUL: case BinOp::DIV: case BinOp::MOD:
                if (!is_numeric(lt) || !is_numeric(rt))
                    throw TypeError("Arithmetic requires numeric operands, got "
                                    + lt.str() + " and " + rt.str(), expr.line);
                expr.resolved_type = widen(lt, rt, expr.line); break;
            case BinOp::EQ: case BinOp::NEQ:
                expr.resolved_type = TypeRef(TypeKind::BOOL); break;
            case BinOp::LT: case BinOp::GT: case BinOp::LEQ: case BinOp::GEQ:
                if (!is_numeric(lt) || !is_numeric(rt))
                    throw TypeError("Comparison requires numeric operands", expr.line);
                expr.resolved_type = TypeRef(TypeKind::BOOL); break;
            case BinOp::AND: case BinOp::OR:
                if (lt.kind != TypeKind::BOOL || rt.kind != TypeKind::BOOL)
                    throw TypeError("Logical operators require bool operands", expr.line);
                expr.resolved_type = TypeRef(TypeKind::BOOL); break;
        }
        break;
    }

    case Expr::Kind::Unary: {
        TypeRef ot = check_expr(*expr.operand);
        if (expr.un_op == UnOp::NEG) {
            if (!is_numeric(ot))
                throw TypeError("Unary '-' requires numeric operand", expr.line);
            expr.resolved_type = ot.clone();
        } else {
            if (ot.kind != TypeKind::BOOL)
                throw TypeError("Unary '!' requires bool operand", expr.line);
            expr.resolved_type = TypeRef(TypeKind::BOOL);
        }
        break;
    }

    case Expr::Kind::Call: {
        /* Try built-in first */
        auto bi = check_builtin(expr.callee_name, expr.args, expr.line);
        if (bi) { expr.resolved_type = bi->clone(); break; }

        auto fit = functions.find(expr.callee_name);
        if (fit == functions.end())
            throw TypeError("Undefined function '" + expr.callee_name + "'", expr.line);
        const FnInfo& fn = fit->second;
        if (expr.args.size() != fn.param_types.size())
            throw TypeError("Function '" + expr.callee_name + "' expects " +
                            std::to_string(fn.param_types.size()) + " argument(s), got " +
                            std::to_string(expr.args.size()), expr.line);
        for (size_t i = 0; i < expr.args.size(); ++i) {
            TypeRef at = check_expr(*expr.args[i]);
            if (!compatible(at, fn.param_types[i]))
                throw TypeError("Argument " + std::to_string(i+1) + " of '" +
                                expr.callee_name + "': expected " +
                                fn.param_types[i].str() + ", got " + at.str(), expr.line);
        }
        expr.resolved_type = fn.return_type.clone(); break;
    }

    case Expr::Kind::MethodCall: {
        TypeRef obj_type = check_expr(*expr.object);
        if (obj_type.kind != TypeKind::CLASS)
            throw TypeError("Method call on non-class type " + obj_type.str(), expr.line);
        FnDecl* method = find_method(obj_type.class_name, expr.method_name, expr.line);
        /* params[0] is 'self', skip it */
        size_t expected = method->params.size();
        if (expected > 0 && method->params[0].name == "self") expected--;
        if (expr.args.size() != expected)
            throw TypeError("Method '" + expr.method_name + "' expects " +
                            std::to_string(expected) + " argument(s)", expr.line);
        size_t param_offset = (method->params.size() > expected) ? 1 : 0;
        for (size_t i = 0; i < expr.args.size(); ++i) {
            TypeRef at = check_expr(*expr.args[i]);
            const TypeRef& pt = method->params[i + param_offset].type;
            if (!compatible(at, pt))
                throw TypeError("Argument " + std::to_string(i+1) + " of method '" +
                                expr.method_name + "': expected " + pt.str() +
                                ", got " + at.str(), expr.line);
        }
        expr.resolved_type = method->return_type.clone(); break;
    }

    case Expr::Kind::StaticCall: {
        /* Resolve as a method without self */
        FnDecl* method = find_method(expr.class_name, expr.method_name, expr.line);
        size_t expected = method->params.size();
        /* static calls should NOT have a 'self' parameter */
        if (expected > 0 && method->params[0].name == "self") {
            throw TypeError("'" + expr.method_name + "' is an instance method; "
                            "call it on an instance, not via '::'", expr.line);
        }
        if (expr.args.size() != expected)
            throw TypeError("Static method '" + expr.method_name + "' expects " +
                            std::to_string(expected) + " argument(s)", expr.line);
        for (size_t i = 0; i < expr.args.size(); ++i) {
            TypeRef at = check_expr(*expr.args[i]);
            if (!compatible(at, method->params[i].type))
                throw TypeError("Argument " + std::to_string(i+1) +
                                " type mismatch in static call", expr.line);
        }
        expr.resolved_type = method->return_type.clone(); break;
    }

    case Expr::Kind::FieldAccess: {
        TypeRef obj_type = check_expr(*expr.object);
        if (obj_type.kind != TypeKind::CLASS)
            throw TypeError("Field access on non-class type " + obj_type.str(), expr.line);
        expr.resolved_type = field_type(obj_type.class_name, expr.str_val, expr.line);
        break;
    }

    case Expr::Kind::Index: {
        TypeRef arr_type = check_expr(*expr.left);
        TypeRef idx_type = check_expr(*expr.index);
        if (idx_type.kind != TypeKind::INT)
            throw TypeError("Array index must be int, got " + idx_type.str(), expr.line);
        if (arr_type.kind != TypeKind::ARRAY)
            throw TypeError("Cannot index non-array type " + arr_type.str(), expr.line);
        if (!arr_type.elem)
            throw TypeError("Array has unknown element type", expr.line);
        expr.resolved_type = arr_type.elem->clone(); break;
    }

    case Expr::Kind::Assign: {
        TypeRef rhs = check_expr(*expr.right);
        /* Determine lhs type */
        TypeRef lhs;
        if (expr.left->kind == Expr::Kind::Ident) {
            auto t = lookup(expr.left->str_val);
            if (!t) throw TypeError("Undefined variable '" + expr.left->str_val + "'",
                                    expr.line);
            lhs = t->clone();
            expr.left->resolved_type = lhs.clone();
        } else {
            lhs = check_expr(*expr.left);
        }
        if (!compatible(rhs, lhs))
            throw TypeError("Cannot assign " + rhs.str() + " to " + lhs.str(), expr.line);
        expr.resolved_type = lhs.clone(); break;
    }

    case Expr::Kind::CompAssign: {
        TypeRef lhs, rhs;
        if (expr.left->kind == Expr::Kind::Ident) {
            auto t = lookup(expr.left->str_val);
            if (!t) throw TypeError("Undefined variable '" + expr.left->str_val + "'",
                                    expr.line);
            lhs = t->clone();
            expr.left->resolved_type = lhs.clone();
        } else {
            lhs = check_expr(*expr.left);
        }
        rhs = check_expr(*expr.right);
        if (!is_numeric(lhs) || !is_numeric(rhs))
            throw TypeError("Compound assignment requires numeric types", expr.line);
        expr.resolved_type = lhs.clone(); break;
    }

    case Expr::Kind::ArrayLit: {
        if (expr.elements.empty()) {
            expr.resolved_type = TypeRef::array_of(TypeRef(TypeKind::INFERRED));
            break;
        }
        TypeRef elem = check_expr(*expr.elements[0]);
        for (size_t i = 1; i < expr.elements.size(); ++i) {
            TypeRef et = check_expr(*expr.elements[i]);
            if (!compatible(et, elem))
                throw TypeError("Array literal has mixed types: " +
                                elem.str() + " and " + et.str(), expr.line);
        }
        expr.resolved_type = TypeRef::array_of(elem.clone()); break;
    }

    case Expr::Kind::ClassCons: {
        const ClassInfo* cls = find_class(expr.class_name, expr.line);
        for (auto& fi : expr.field_inits) {
            auto it = cls->fields.find(fi.name);
            if (it == cls->fields.end())
                throw TypeError("Class '" + expr.class_name +
                                "' has no field '" + fi.name + "'", expr.line);
            TypeRef vt = check_expr(*fi.value);
            if (!compatible(vt, it->second))
                throw TypeError("Field '" + fi.name + "' expects " +
                                it->second.str() + ", got " + vt.str(), expr.line);
        }
        expr.resolved_type = TypeRef(expr.class_name); break;
    }

    case Expr::Kind::Range:
        check_expr(*expr.left);
        check_expr(*expr.right);
        expr.resolved_type = TypeRef("__Range"); break;
    }

    return expr.resolved_type.clone();
}
