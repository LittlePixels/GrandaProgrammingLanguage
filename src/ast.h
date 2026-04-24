#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

/* Forward declarations */
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

/* -----------------------------------------------------------------------
 * Type representation
 * --------------------------------------------------------------------- */
enum class TypeKind {
    INT, FLOAT, STR, BOOL, VOID,
    ARRAY,   /* element type in ->elem */
    CLASS,   /* class_name set */
    INFERRED,
};

struct TypeRef {
    TypeKind   kind       = TypeKind::INFERRED;
    std::string class_name;
    std::unique_ptr<TypeRef> elem; /* for ARRAY */

    TypeRef() = default;
    explicit TypeRef(TypeKind k) : kind(k) {}
    explicit TypeRef(std::string name)
        : kind(TypeKind::CLASS), class_name(std::move(name)) {}

    static TypeRef array_of(TypeRef e) {
        TypeRef t; t.kind = TypeKind::ARRAY;
        t.elem = std::make_unique<TypeRef>(std::move(e)); return t;
    }

    TypeRef clone() const {
        TypeRef t; t.kind = kind; t.class_name = class_name;
        if (elem) t.elem = std::make_unique<TypeRef>(elem->clone());
        return t;
    }

    bool operator==(const TypeRef& o) const {
        if (kind != o.kind) return false;
        if (kind == TypeKind::CLASS) return class_name == o.class_name;
        if (kind == TypeKind::ARRAY)
            return elem && o.elem && *elem == *o.elem;
        return true;
    }
    bool operator!=(const TypeRef& o) const { return !(*this == o); }

    bool is_gc() const {
        return kind == TypeKind::STR || kind == TypeKind::ARRAY
            || kind == TypeKind::CLASS;
    }

    std::string str() const {
        switch (kind) {
            case TypeKind::INT:      return "int";
            case TypeKind::FLOAT:    return "float";
            case TypeKind::STR:      return "str";
            case TypeKind::BOOL:     return "bool";
            case TypeKind::VOID:     return "void";
            case TypeKind::ARRAY:    return "[" + (elem ? elem->str() : "?") + "]";
            case TypeKind::CLASS:    return class_name;
            case TypeKind::INFERRED: return "<inferred>";
        }
        return "?";
    }
};

/* -----------------------------------------------------------------------
 * Parameters and field-init pairs
 * --------------------------------------------------------------------- */
struct Param {
    std::string name;
    TypeRef     type;
    int         line = 0;
};

struct FieldInit {
    std::string name;
    ExprPtr     value;
};

/* -----------------------------------------------------------------------
 * Binary / unary operators
 * --------------------------------------------------------------------- */
enum class BinOp {
    ADD, SUB, MUL, DIV, MOD,
    EQ, NEQ, LT, GT, LEQ, GEQ,
    AND, OR,
};

enum class UnOp { NEG, NOT };

/* -----------------------------------------------------------------------
 * Expression node
 * --------------------------------------------------------------------- */
struct Expr {
    int     line = 0;
    TypeRef resolved_type; /* filled in by the type checker */

    enum class Kind {
        IntLit, FloatLit, StrLit, BoolLit, NullLit,
        Ident, SelfExpr,
        Binary, Unary,
        Call,         /* fn(args) */
        MethodCall,   /* obj.method(args) */
        StaticCall,   /* Class::method(args) */
        FieldAccess,  /* obj.field */
        Index,        /* arr[i] */
        Assign,       /* target = value */
        CompAssign,   /* target op= value */
        ArrayLit,     /* [a, b, c] */
        ClassCons,    /* ClassName { field: val, ... } */
        Range,        /* start..end */
    } kind = Kind::IntLit;

    /* literal values */
    std::string str_val;
    long long   int_val   = 0;
    double      float_val = 0.0;
    bool        bool_val  = false;

    /* binary / unary */
    BinOp   bin_op = BinOp::ADD;
    UnOp    un_op  = UnOp::NEG;
    ExprPtr left, right;   /* binary */
    ExprPtr operand;       /* unary */

    /* call / method / static */
    std::string callee_name;
    ExprPtr     object;
    std::string class_name;
    std::string method_name;
    std::vector<ExprPtr> args;

    /* index */
    ExprPtr index;

    /* compound assign */
    BinOp comp_op = BinOp::ADD;

    /* array literal */
    std::vector<ExprPtr> elements;

    /* class construction */
    std::vector<FieldInit> field_inits;
};

/* -----------------------------------------------------------------------
 * Statement node
 * --------------------------------------------------------------------- */
struct Stmt {
    int line = 0;

    enum class Kind {
        VarDecl, ExprStmt,
        If, While, For,
        Return, Break, Continue,
    } kind = Kind::ExprStmt;

    /* VarDecl */
    std::string var_name;
    TypeRef     var_type;
    ExprPtr     var_init;

    /* ExprStmt (also covers Assign/CompAssign) */
    ExprPtr expr;

    /* If */
    struct Branch {
        ExprPtr            cond;
        std::vector<StmtPtr> body;
    };
    std::vector<Branch>  if_branches;
    std::vector<StmtPtr> else_body;

    /* While */
    ExprPtr              while_cond;
    std::vector<StmtPtr> body;

    /* For */
    std::string          for_var;
    ExprPtr              for_iter;

    /* Return */
    ExprPtr ret_val;
};

/* -----------------------------------------------------------------------
 * Declaration nodes
 * --------------------------------------------------------------------- */
struct FieldDecl {
    std::string name;
    TypeRef     type;
    int         line = 0;
};

struct FnDecl {
    bool        is_pub     = false;
    bool        is_method  = false;
    std::string name;
    std::vector<Param> params;
    TypeRef     return_type;
    std::vector<StmtPtr> body;
    int         line = 0;
};

struct ClassDecl {
    bool        is_pub = false;
    std::string name;
    std::string base_class;
    std::vector<FieldDecl> fields;
    std::vector<FnDecl>    methods;
    int         line = 0;
};

struct ImportDecl {
    std::string path;
    int         line = 0;
};

struct Program {
    std::vector<ImportDecl> imports;
    std::vector<ClassDecl>  classes;
    std::vector<FnDecl>     functions;
};
