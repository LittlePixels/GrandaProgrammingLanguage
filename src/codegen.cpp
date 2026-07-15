#include "codegen.h"
#include <sstream>
#include <algorithm>
#include <cassert>

/* -----------------------------------------------------------------------
 * Infrastructure
 * --------------------------------------------------------------------- */
static std::string indent_str(int n) { return std::string((size_t)n * 4, ' '); }

void Codegen::iout(const std::string& s) {
    out << indent_str(indent) << s << '\n';
}

std::string Codegen::fresh_tmp() {
    return "_t" + std::to_string(tmp_counter++);
}

std::string Codegen::mangle(const std::string& cls, const std::string& fn) const {
    return cls + "_" + fn;
}

std::string Codegen::get_vtable_root(const std::string& class_name) const {
    auto it = vtable_root.find(class_name);
    return (it != vtable_root.end()) ? it->second : "";
}

bool Codegen::class_needs_vtable(const std::string& class_name) const {
    return !get_vtable_root(class_name).empty();
}

const FnDecl* Codegen::find_method_decl(const std::string& class_name,
                                         const std::string& method_name) const {
    auto it = class_decls.find(class_name);
    if (it == class_decls.end()) return nullptr;
    /* Search the class and its bases */
    const ClassDecl* cls = it->second;
    while (cls) {
        for (auto& m : cls->methods)
            if (m.name == method_name) return &m;
        if (!cls->base_class.empty()) {
            auto bit = class_decls.find(cls->base_class);
            cls = (bit != class_decls.end()) ? bit->second : nullptr;
        } else {
            cls = nullptr;
        }
    }
    return nullptr;
}

std::string Codegen::find_defining_class(const std::string& class_name,
                                         const std::string& method_name) const {
    auto it = class_decls.find(class_name);
    if (it == class_decls.end()) return class_name;
    const ClassDecl* cls = it->second;
    while (cls) {
        for (auto& m : cls->methods)
            if (m.name == method_name) return cls->name;
        if (!cls->base_class.empty()) {
            auto bit = class_decls.find(cls->base_class);
            cls = (bit != class_decls.end()) ? bit->second : nullptr;
        } else {
            cls = nullptr;
        }
    }
    return class_name;
}

bool Codegen::is_subclass(const std::string& derived, const std::string& base) const {
    std::string cur = derived;
    while (!cur.empty()) {
        if (cur == base) return true;
        auto it = class_decls.find(cur);
        if (it == class_decls.end()) break;
        cur = it->second->base_class;
    }
    return false;
}

/* -----------------------------------------------------------------------
 * C type mapping
 * --------------------------------------------------------------------- */
std::string Codegen::c_type(const TypeRef& t) {
    switch (t.kind) {
        case TypeKind::INT:   return "int64_t";
        case TypeKind::FLOAT: return "double";
        case TypeKind::BOOL:  return "int";
        case TypeKind::VOID:  return "void";
        case TypeKind::STR:   return "GrandaStr*";
        case TypeKind::ARRAY: return "GrandaArray*";
        case TypeKind::CLASS: return t.class_name + "*";
        case TypeKind::TRAIT: return "void*";
        default:              return "void*";
    }
}

std::string Codegen::c_default(const TypeRef& t) {
    switch (t.kind) {
        case TypeKind::INT:   return "0";
        case TypeKind::FLOAT: return "0.0";
        case TypeKind::BOOL:  return "0";
        default:              return "NULL";
    }
}

bool Codegen::type_is_gc(const TypeRef& t) const {
    return t.kind == TypeKind::STR || t.kind == TypeKind::ARRAY
        || t.kind == TypeKind::CLASS;
}

bool Codegen::class_has_gc_fields(const ClassDecl& cls) const {
    for (auto& f : cls.fields)
        if (type_is_gc(f.type)) return true;
    if (!cls.base_class.empty()) {
        auto it = class_decls.find(cls.base_class);
        if (it != class_decls.end() && class_has_gc_fields(*it->second))
            return true;
    }
    return false;
}

std::string Codegen::bin_op_str(BinOp op, const TypeRef& rt) {
    (void)rt;
    switch (op) {
        case BinOp::ADD: return "+";
        case BinOp::SUB: return "-";
        case BinOp::MUL: return "*";
        case BinOp::DIV: return "/";
        case BinOp::MOD: return "%";
        case BinOp::EQ:  return "==";
        case BinOp::NEQ: return "!=";
        case BinOp::LT:  return "<";
        case BinOp::GT:  return ">";
        case BinOp::LEQ: return "<=";
        case BinOp::GEQ: return ">=";
        case BinOp::AND: return "&&";
        case BinOp::OR:  return "||";
    }
    return "+";
}

/* -----------------------------------------------------------------------
 * Main entry
 * --------------------------------------------------------------------- */
std::string Codegen::emit(const Program& prog) {
    /* Index classes */
    for (auto& cls : prog.classes)
        class_decls[cls.name] = &cls;

    /* Index functions */
    for (auto& fn : prog.functions)
        fn_decls[fn.name] = &fn;

    /* Index traits */
    for (auto& trait : prog.traits) {
        trait_decls[trait.name] = &trait;
        for (auto& m : trait.methods)
            trait_method_list[trait.name].push_back(m.name);
    }

    /* Compute vtable roots: walk up inheritance to find first class with virtual methods */
    for (auto& cls : prog.classes) {
        std::string cur = cls.name;
        std::string root = "";
        while (!cur.empty()) {
            auto it = class_decls.find(cur);
            if (it == class_decls.end()) break;
            for (auto& m : it->second->methods) {
                if (m.is_virtual) {
                    root = cur;
                    break;
                }
            }
            if (!root.empty()) break;
            cur = it->second->base_class;
        }
        if (!root.empty()) {
            vtable_root[cls.name] = root;
            /* Populate vtable method list from root if not already done */
            if (vtable_method_list.find(root) == vtable_method_list.end()) {
                auto rit = class_decls.find(root);
                if (rit != class_decls.end()) {
                    for (auto& m : rit->second->methods)
                        if (m.is_virtual)
                            vtable_method_list[root].push_back(m.name);
                    /* Also include methods made virtual in subclasses */
                    for (auto& c : prog.classes) {
                        if (c.base_class == root || /* direct child */
                            (!c.base_class.empty() && vtable_root[c.name] == root)) {
                            for (auto& m : c.methods)
                                if (m.is_virtual && !m.is_override) {
                                    bool found = false;
                                    for (auto& vm : vtable_method_list[root])
                                        if (vm == m.name) { found = true; break; }
                                    if (!found) vtable_method_list[root].push_back(m.name);
                                }
                        }
                    }
                }
            }
        }
    }

    /* --- preamble --- */
    out << "/* Generated by the Granda compiler 0.2.0 */\n";
    out << "#include <stdint.h>\n";
    out << "#include <stdio.h>\n";
    out << "#include <stdlib.h>\n";
    out << "#include <string.h>\n";
    out << "#include \"granda_rt.h\"\n\n";
    out << "typedef void (*GrandaVtableFn)(void*);\n\n";

    /* --- forward declarations for all classes --- */
    for (auto& cls : prog.classes) emit_class_forward(cls);
    /* --- forward declarations for all traits (opaque pointer types) --- */
    for (auto& trait : prog.traits)
        out << "typedef struct " << trait.name << "_s " << trait.name << ";\n";
    out << '\n';

    /* --- vtable struct typedefs --- */
    std::unordered_set<std::string> emitted_vtables;
    for (auto& cls : prog.classes) {
        auto vr = vtable_root.find(cls.name);
        if (vr != vtable_root.end() && vr->second == cls.name
            && emitted_vtables.find(cls.name) == emitted_vtables.end()) {
            emit_vtable_struct(cls.name);
            emitted_vtables.insert(cls.name);
        }
    }

    /* --- trait vtable struct typedefs --- */
    for (auto& [tname, tmethods] : trait_method_list) {
        emit_trait_vtable_struct(tname);
    }

    /* --- forward-declare vtable instances (alloc functions need them) --- */
    for (auto& cls : prog.classes) {
        auto vr = vtable_root.find(cls.name);
        if (vr != vtable_root.end()) {
            std::string root = vr->second;
            out << "static " << root << "_Vtable " << cls.name << "_vtable;\n";
        }
    }

    /* --- forward-declare trait vtable instances --- */
    for (auto& cls : prog.classes) {
        for (auto& tname : cls.implements) {
            out << "static " << tname << "_Vtable " << cls.name
                << "_" << tname << "_vtable;\n";
        }
    }

    /* --- forward-declare trait default wrappers (needed by vtable instances) --- */
    for (auto& trait : prog.traits) {
        auto tmit = trait_method_list.find(trait.name);
        if (tmit == trait_method_list.end()) continue;
        for (auto& mname : tmit->second) {
            auto tmit2 = trait_decls.find(trait.name);
            if (tmit2 == trait_decls.end()) continue;
            const FnDecl* method = nullptr;
            for (auto& m : tmit2->second->methods)
                if (m.name == mname) { method = &m; break; }
            if (!method || method->body.empty()) continue;
            /* Emit forward declaration for default wrapper */
            out << "static " << c_type(method->return_type) << " "
                << trait.name << "_" << mname << "_default_vwrap(";
            bool first = true;
            int pidx = 0;
            for (auto& p : method->params) {
                if (!first) out << ", "; first = false;
                out << "void* _p" << pidx;
                pidx++;
            }
            if (first) out << "void";
            out << ");\n";
        }
    }
    out << '\n';

    /* --- class structs + alloc helpers --- */
    for (auto& cls : prog.classes) {
        emit_class_struct(cls);
        emit_class_alloc(cls);
        out << '\n';
    }

    /* --- forward-declare all user functions (so order doesn't matter) --- */
    for (auto& cls : prog.classes)
        for (auto& m : cls.methods)
            emit_fn_decl(m, cls.name);
    for (auto& fn : prog.functions)
        emit_fn_decl(fn);
    /* Forward-declare trait default implementations */
    for (auto& trait : prog.traits) {
        current_trait = trait.name;
        for (auto& m : trait.methods)
            if (!m.body.empty())
                emit_fn_decl(m);
        current_trait = "";
    }
    out << '\n';

    /* --- vtable wrapper functions and instances (need method forward decls) --- */
    for (auto& cls : prog.classes) {
        auto vr = vtable_root.find(cls.name);
        if (vr == vtable_root.end()) continue;
        auto mlit = vtable_method_list.find(vr->second);
        if (mlit != vtable_method_list.end()) {
            for (auto& mname : mlit->second)
                emit_vtable_wrapper(cls, mname);
        }
        emit_vtable_instance(cls);
        out << '\n';
    }

    /* --- trait vtable wrapper functions and instances --- */
    for (auto& cls : prog.classes) {
        for (auto& tname : cls.implements) {
            auto tmit = trait_method_list.find(tname);
            if (tmit != trait_method_list.end()) {
                for (auto& mname : tmit->second)
                    emit_trait_vtable_wrapper(cls.name, tname, mname);
            }
            emit_trait_vtable_instance(cls.name, tname);
            out << '\n';
        }
    }

    /* --- trait default wrapper functions --- */
    for (auto& trait : prog.traits) {
        auto tmit = trait_method_list.find(trait.name);
        if (tmit != trait_method_list.end()) {
            for (auto& mname : tmit->second)
                emit_trait_default_wrapper(trait.name, mname);
        }
    }

    /* --- function bodies --- */
    for (auto& cls : prog.classes)
        for (auto& m : cls.methods)
            emit_fn_body(m, cls.name);
    for (auto& fn : prog.functions)
        emit_fn_body(fn);
    /* Emit trait default implementations */
    for (auto& trait : prog.traits) {
        current_trait = trait.name;
        for (auto& m : trait.methods)
            if (!m.body.empty())
                emit_fn_body(m);
        current_trait = "";
    }

    /* --- C main --- */
    out << "\nint main(int argc, char* argv[]) {\n";
    out << "    gc_init();\n";
    out << "    granda_set_args(argc, argv);\n";
    out << "    granda_main();\n";
    out << "    gc_shutdown();\n";
    out << "    return 0;\n";
    out << "}\n";

    return out.str();
}

/* -----------------------------------------------------------------------
 * Class emission
 * --------------------------------------------------------------------- */
void Codegen::emit_class_forward(const ClassDecl& cls) {
    out << "typedef struct " << cls.name << "_s " << cls.name << ";\n";
}

void Codegen::emit_class_struct(const ClassDecl& cls) {
    out << "struct " << cls.name << "_s {\n";
    out << "    GC_Header _gc;\n";
    out << "    void* _vtable;\n";
    if (!cls.base_class.empty())
        out << "    /* base fields from " << cls.base_class << " */\n";

    /* Collect all fields including inherited */
    std::vector<const FieldDecl*> all_fields;
    /* First add base fields */
    if (!cls.base_class.empty()) {
        auto it = class_decls.find(cls.base_class);
        if (it != class_decls.end())
            for (auto& f : it->second->fields)
                all_fields.push_back(&f);
    }
    for (auto& f : cls.fields) all_fields.push_back(&f);

    for (auto* f : all_fields)
        out << "    " << c_type(f->type) << " " << f->name << ";\n";

    /* Trait vtable pointers */
    for (auto& tname : cls.implements)
        out << "    " << tname << "_Vtable* _vtable_" << tname << ";\n";

    out << "};\n";
}

void Codegen::emit_class_alloc(const ClassDecl& cls) {
    bool has_gc = class_has_gc_fields(cls);

    /* trace function */
    if (has_gc) {
        out << "static void " << cls.name << "_gc_trace(GC_Header* _h) {\n";
        out << "    " << cls.name << "* self = (" << cls.name << "*)_h;\n";

        /* Trace base class fields too */
        if (!cls.base_class.empty()) {
            auto it = class_decls.find(cls.base_class);
            if (it != class_decls.end())
                for (auto& f : it->second->fields)
                    if (type_is_gc(f.type))
                        out << "    rc_retain(self->" << f.name << ");\n";
        }
        for (auto& f : cls.fields)
            if (type_is_gc(f.type))
                out << "    rc_retain(self->" << f.name << ");\n";
        out << "}\n";
    }

    /* free function */
    out << "static void " << cls.name << "_gc_free(GC_Header* _h) {\n";
    out << "    " << cls.name << "* self = (" << cls.name << "*)_h;\n";

    /* Release base class GC fields */
    if (!cls.base_class.empty()) {
        auto it = class_decls.find(cls.base_class);
        if (it != class_decls.end())
            for (auto& f : it->second->fields)
                if (type_is_gc(f.type))
                    out << "    rc_release(self->" << f.name << ");\n";
    }
    for (auto& f : cls.fields)
        if (type_is_gc(f.type))
            out << "    rc_release(self->" << f.name << ");\n";
    out << "    free(self);\n";
    out << "}\n";

    /* alloc helper */
    out << "static " << cls.name << "* " << cls.name << "_alloc(void) {\n";
    out << "    " << cls.name << "* _obj = (" << cls.name
        << "*)malloc(sizeof(" << cls.name << "));\n";
    out << "    if (!_obj) granda_panic(\"Out of memory allocating "
        << cls.name << "\");\n";
    out << "    memset(_obj, 0, sizeof(" << cls.name << "));\n";
    out << "    _obj->_gc.ref_count = 1;\n";
    out << "    _obj->_gc.type_tag  = GRANDA_TAG_CLASS;\n";
    out << "    _obj->_gc.trace     = "
        << (has_gc ? cls.name + "_gc_trace" : "NULL") << ";\n";
    out << "    _obj->_gc.free_fn   = " << cls.name << "_gc_free;\n";
    /* Set vtable pointer */
    auto vr = vtable_root.find(cls.name);
    if (vr != vtable_root.end())
        out << "    _obj->_vtable = (void*)&" << cls.name << "_vtable;\n";
    else
        out << "    _obj->_vtable = NULL;\n";
    /* Set trait vtable pointers */
    for (auto& tname : cls.implements)
        out << "    _obj->_vtable_" << tname << " = &" << cls.name
            << "_" << tname << "_vtable;\n";
    out << "    return _obj;\n";
    out << "}\n";
}

/* -----------------------------------------------------------------------
 * Vtable emission
 * --------------------------------------------------------------------- */
void Codegen::emit_vtable_struct(const std::string& root_class) {
    auto mlit = vtable_method_list.find(root_class);
    if (mlit == vtable_method_list.end()) return;

    out << "typedef struct " << root_class << "_Vtable {\n";
    for (auto& mname : mlit->second) {
        const FnDecl* m = find_method_decl(root_class, mname);
        if (!m) continue;
        out << "    " << c_type(m->return_type) << " (*" << mname << ")(";
        bool first = true;
        for (auto& p : m->params) {
            if (!first) out << ", "; first = false;
            if (p.name == "self") out << "void*";
            else out << c_type(p.type);
        }
        if (first) out << "void";
        out << ");\n";
    }
    out << "} " << root_class << "_Vtable;\n\n";
}

void Codegen::emit_vtable_wrapper(const ClassDecl& cls, const std::string& method_name) {
    auto vr = vtable_root.find(cls.name);
    if (vr == vtable_root.end()) return;

    const FnDecl* m = find_method_decl(cls.name, method_name);
    if (!m) return;

    /* Generate wrapper: RetType Class_method_vwrap(void* _p0, ActualType _p1, ...) */
    out << "static " << c_type(m->return_type) << " "
        << cls.name << "_" << method_name << "_vwrap(";
    bool first = true;
    int param_idx = 0;
    for (auto& p : m->params) {
        if (!first) out << ", "; first = false;
        if (p.name == "self") out << "void* _p" << param_idx;
        else out << c_type(p.type) << " _p" << param_idx;
        param_idx++;
    }
    if (first) out << "void";
    out << ") {\n";
    ++indent;

    /* Cast self back to proper type */
    iout(cls.name + "* _self = (" + cls.name + "*)_p0;");

    /* Build call with proper types */
    std::string call = mangle(cls.name, method_name) + "(_self";
    for (size_t i = 0; i < m->params.size(); ++i) {
        if (m->params[i].name == "self") continue;
        call += ", _p" + std::to_string((int)i);
    }
    call += ")";

    if (m->return_type.kind != TypeKind::VOID)
        iout("return " + call + ";");
    else
        iout(call + ";");

    --indent;
    iout("}\n");
}

void Codegen::emit_vtable_instance(const ClassDecl& cls) {
    auto vr = vtable_root.find(cls.name);
    if (vr == vtable_root.end()) return;
    const std::string& root = vr->second;

    auto mlit = vtable_method_list.find(root);
    if (mlit == vtable_method_list.end()) return;

    out << "static " << root << "_Vtable " << cls.name << "_vtable = {\n";
    ++indent;
    bool first = true;
    for (auto& mname : mlit->second) {
        if (!first) out << ",\n"; first = false;
        iout("." + mname + " = " + cls.name + "_" + mname + "_vwrap");
    }
    out << "\n";
    --indent;
    iout("};\n");
}

/* -----------------------------------------------------------------------
 * Trait vtable emission
 * --------------------------------------------------------------------- */
void Codegen::emit_trait_vtable_struct(const std::string& trait_name) {
    auto mlit = trait_method_list.find(trait_name);
    if (mlit == trait_method_list.end()) return;

    out << "typedef struct " << trait_name << "_Vtable {\n";
    for (auto& mname : mlit->second) {
        auto tmit = trait_decls.find(trait_name);
        if (tmit == trait_decls.end()) continue;
        const FnDecl* m = nullptr;
        for (auto& method : tmit->second->methods)
            if (method.name == mname) { m = &method; break; }
        if (!m) continue;
        out << "    " << c_type(m->return_type) << " (*" << mname << ")(";
        bool first = true;
        for (auto& p : m->params) {
            if (!first) out << ", "; first = false;
            if (p.name == "self") out << "void*";
            else out << c_type(p.type);
        }
        if (first) out << "void";
        out << ");\n";
    }
    out << "} " << trait_name << "_Vtable;\n\n";
}

void Codegen::emit_trait_vtable_wrapper(const std::string& class_name,
                                         const std::string& trait_name,
                                         const std::string& method_name) {
    /* Find the trait method signature */
    auto tmit = trait_decls.find(trait_name);
    if (tmit == trait_decls.end()) return;
    const FnDecl* trait_method = nullptr;
    for (auto& m : tmit->second->methods)
        if (m.name == method_name) { trait_method = &m; break; }
    if (!trait_method) return;

    /* Find the class's implementation of this method */
    const FnDecl* class_method = find_method_decl(class_name, method_name);
    if (!class_method) return;

    /* Generate wrapper: RetType TraitName_ClassName_method_vwrap(void* _p0, ...) */
    out << "static " << c_type(trait_method->return_type) << " "
        << trait_name << "_" << class_name << "_" << method_name << "_vwrap(";
    bool first = true;
    int param_idx = 0;
    for (auto& p : trait_method->params) {
        if (!first) out << ", "; first = false;
        if (p.name == "self") out << "void* _p" << param_idx;
        else out << c_type(p.type) << " _p" << param_idx;
        param_idx++;
    }
    if (first) out << "void";
    out << ") {\n";
    ++indent;

    /* Cast self back to proper type */
    iout(class_name + "* _self = (" + class_name + "*)_p0;");

    /* Build call with proper types */
    std::string call = mangle(class_name, method_name) + "(_self";
    for (size_t i = 0; i < class_method->params.size(); ++i) {
        if (class_method->params[i].name == "self") continue;
        call += ", _p" + std::to_string((int)i);
    }
    call += ")";

    if (trait_method->return_type.kind != TypeKind::VOID)
        iout("return " + call + ";");
    else
        iout(call + ";");

    --indent;
    iout("}\n");
}

void Codegen::emit_trait_default_wrapper(const std::string& trait_name,
                                         const std::string& method_name) {
    /* Find the trait method signature */
    auto tmit = trait_decls.find(trait_name);
    if (tmit == trait_decls.end()) return;
    const FnDecl* trait_method = nullptr;
    for (auto& m : tmit->second->methods)
        if (m.name == method_name) { trait_method = &m; break; }
    if (!trait_method || trait_method->body.empty()) return;

    /* Emit default wrapper: TraitName_methodName_default_vwrap(void* _p0, ...) */
    out << "static " << c_type(trait_method->return_type) << " "
        << trait_name << "_" << method_name << "_default_vwrap(";
    bool first = true;
    int param_idx = 0;
    for (auto& p : trait_method->params) {
        if (!first) out << ", "; first = false;
        if (p.name == "self") out << "void* _p" << param_idx;
        else out << c_type(p.type) << " _p" << param_idx;
        param_idx++;
    }
    if (first) out << "void";
    out << ") {\n";
    ++indent;

    /* Build call to the default implementation */
    std::string call = mangle(trait_name, method_name) + "(("
        + trait_name + "*)_p0";
    for (size_t i = 1; i < trait_method->params.size(); ++i) {
        call += ", _p" + std::to_string((int)i);
    }
    call += ")";

    if (trait_method->return_type.kind != TypeKind::VOID)
        iout("return " + call + ";");
    else
        iout(call + ";");

    --indent;
    iout("}\n");
}

void Codegen::emit_trait_vtable_instance(const std::string& class_name,
                                          const std::string& trait_name) {
    auto mlit = trait_method_list.find(trait_name);
    if (mlit == trait_method_list.end()) return;

    out << "static " << trait_name << "_Vtable " << class_name
        << "_" << trait_name << "_vtable = {\n";
    ++indent;
    bool first = true;
    for (auto& mname : mlit->second) {
        if (!first) out << ",\n"; first = false;

        /* Check if the class defines its own implementation of this method */
        bool class_has_own = false;
        auto cd = class_decls.find(class_name);
        if (cd != class_decls.end()) {
            for (auto& m : cd->second->methods)
                if (m.name == mname) { class_has_own = true; break; }
        }

        if (class_has_own) {
            iout("." + mname + " = " + trait_name + "_" + class_name
                 + "_" + mname + "_vwrap");
        } else {
            iout("." + mname + " = " + trait_name + "_" + mname
                 + "_default_vwrap");
        }
    }
    out << "\n";
    --indent;
    iout("};\n");
}

bool Codegen::class_implements_trait(const std::string& class_name,
                                      const std::string& trait_name) const {
    auto it = class_decls.find(class_name);
    if (it == class_decls.end()) return false;
    for (auto& t : it->second->implements)
        if (t == trait_name) return true;
    return false;
}

/* -----------------------------------------------------------------------
 * Function declaration (forward decl)
 * --------------------------------------------------------------------- */
void Codegen::emit_fn_decl(const FnDecl& fn, const std::string& class_ctx) {
    /* For trait defaults, use trait name as context so self is trait-typed */
    std::string ctx = class_ctx;
    if (ctx.empty() && !current_trait.empty()) ctx = current_trait;
    std::string mangled = ctx.empty() ? fn.name : mangle(ctx, fn.name);
    /* Rename 'main' → 'granda_main' to avoid conflict with C main */
    if (mangled == "main") mangled = "granda_main";
    /* Replace :: with _ for module names */
    for (auto& c : mangled) if (c == ':') c = '_';

    out << c_type(fn.return_type) << " " << mangled << "(";
    bool first = true;
    for (auto& p : fn.params) {
        if (!first) out << ", "; first = false;
        if (p.name == "self") {
            if (!current_trait.empty())
                out << current_trait << "* self";
            else
                out << class_ctx << "* self";
        } else if (p.type.kind == TypeKind::TRAIT) {
            /* Trait param → hidden vtable pointer + void* */
            out << p.type.class_name << "_Vtable* _" << p.name << "_vt, void* " << p.name;
        } else {
            out << c_type(p.type) << " " << p.name;
        }
    }
    if (first) out << "void";
    out << ");\n";
}

/* -----------------------------------------------------------------------
 * Function body
 * --------------------------------------------------------------------- */
void Codegen::emit_fn_body(const FnDecl& fn, const std::string& class_ctx) {
    current_class = class_ctx;
    tmp_counter = 0;
    trait_param_to_vtable.clear();

    /* For trait defaults, use trait name as context */
    std::string ctx = class_ctx;
    if (ctx.empty() && !current_trait.empty()) ctx = current_trait;
    std::string mangled = ctx.empty() ? fn.name : mangle(ctx, fn.name);
    if (mangled == "main") mangled = "granda_main";
    /* Replace :: with _ for module names */
    for (auto& c : mangled) if (c == ':') c = '_';

    out << c_type(fn.return_type) << " " << mangled << "(";
    bool first = true;
    for (auto& p : fn.params) {
        if (!first) out << ", "; first = false;
        if (p.name == "self") {
            if (!current_trait.empty())
                out << current_trait << "* self";
            else
                out << class_ctx << "* self";
        } else if (p.type.kind == TypeKind::TRAIT) {
            std::string vt_name = "_" + p.name + "_vt";
            out << p.type.class_name << "_Vtable* " << vt_name << ", void* " << p.name;
            trait_param_to_vtable[p.name] = vt_name;
        } else {
            out << c_type(p.type) << " " << p.name;
        }
    }
    if (first) out << "void";
    out << ") {\n";
    indent = 1;
    /* For trait defaults, bind self's vtable for dispatch */
    if (!current_trait.empty()) {
        trait_param_to_vtable["self"] = "_self_vt";
    }
    emit_block(fn.body);
    indent = 0;
    out << "}\n\n";

    current_class = "";
    trait_param_to_vtable.clear();
}

/* -----------------------------------------------------------------------
 * Block and statement
 * --------------------------------------------------------------------- */
void Codegen::emit_block(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) emit_stmt(*s);
}

void Codegen::emit_stmt(const Stmt& stmt) {
    switch (stmt.kind) {

    case Stmt::Kind::VarDecl: {
        std::string ct = c_type(stmt.var_type);
        std::string init = stmt.var_init
            ? emit_expr(*stmt.var_init)
            : c_default(stmt.var_type);
        iout(ct + " " + stmt.var_name + " = " + init + ";");
        break;
    }

    case Stmt::Kind::ExprStmt:
        if (stmt.expr) {
            std::string val = emit_expr(*stmt.expr);
            /* Only emit as a statement if it could have side effects */
            if (!val.empty() && val != "0" && val != "NULL")
                iout("(void)(" + val + ");");
        }
        break;

    case Stmt::Kind::If: {
        for (size_t i = 0; i < stmt.if_branches.size(); ++i) {
            const auto& br = stmt.if_branches[i];
            std::string cond = emit_expr(*br.cond);
            if (i == 0) iout("if (" + cond + ") {");
            else        iout("else if (" + cond + ") {");
            ++indent;
            emit_block(br.body);
            --indent;
            iout("}");
        }
        if (!stmt.else_body.empty()) {
            iout("else {");
            ++indent;
            emit_block(stmt.else_body);
            --indent;
            iout("}");
        }
        break;
    }

    case Stmt::Kind::While: {
        std::string cond = emit_expr(*stmt.while_cond);
        iout("while (" + cond + ") {");
        ++indent;
        emit_block(stmt.body);
        --indent;
        iout("}");
        break;
    }

    case Stmt::Kind::For: {
        /* Range: for i in start..end */
        if (stmt.for_iter->kind == Expr::Kind::Range) {
            std::string start = emit_expr(*stmt.for_iter->left);
            std::string end   = emit_expr(*stmt.for_iter->right);
            iout("for (int64_t " + stmt.for_var + " = " + start + "; "
                 + stmt.for_var + " < " + end + "; ++" + stmt.for_var + ") {");
        } else {
            /* Array iteration */
            std::string arr   = emit_expr(*stmt.for_iter);
            std::string idx   = fresh_tmp();
            std::string len   = fresh_tmp();
            TypeRef elem_type(TypeKind::INFERRED);
            if (stmt.for_iter->resolved_type.kind == TypeKind::ARRAY
                && stmt.for_iter->resolved_type.elem)
                elem_type = stmt.for_iter->resolved_type.elem->clone();
            std::string elem_ct = c_type(elem_type);

            iout("{");
            ++indent;
            iout("int64_t " + len + " = granda_array_len(" + arr + ");");
            iout("for (int64_t " + idx + " = 0; " + idx + " < " + len
                 + "; ++" + idx + ") {");
            ++indent;

            /* element access */
            if (elem_type.kind == TypeKind::INT)
                iout(elem_ct + " " + stmt.for_var + " = granda_int_array_get("
                     + arr + ", " + idx + ");");
            else if (elem_type.kind == TypeKind::FLOAT)
                iout(elem_ct + " " + stmt.for_var + " = granda_float_array_get("
                     + arr + ", " + idx + ");");
            else
                iout(elem_ct + " " + stmt.for_var + " = (" + elem_ct
                     + ")granda_array_get(" + arr + ", " + idx + ");");
        }
        emit_block(stmt.body);
        if (stmt.for_iter->kind != Expr::Kind::Range) {
            --indent;
            iout("}"); /* inner for */
            --indent;
            iout("}"); /* outer block */
        } else {
            --indent;
            iout("}");
        }
        break;
    }

    case Stmt::Kind::Return: {
        if (stmt.ret_val) {
            std::string val = emit_expr(*stmt.ret_val);
            iout("return " + val + ";");
        } else {
            iout("return;");
        }
        break;
    }

    case Stmt::Kind::Break:    iout("break;");    break;
    case Stmt::Kind::Continue: iout("continue;"); break;
    }
}

/* -----------------------------------------------------------------------
 * Expression emission
 *
 * Emits any necessary temp-variable statements directly to `out`, then
 * returns the C expression string representing the result.
 * --------------------------------------------------------------------- */
std::string Codegen::emit_expr(const Expr& expr) {
    switch (expr.kind) {

    case Expr::Kind::IntLit:
        return expr.str_val + "LL";

    case Expr::Kind::FloatLit:
        return expr.str_val;

    case Expr::Kind::StrLit: {
        /* Escape the string for C */
        std::string escaped;
        for (char c : expr.str_val) {
            switch (c) {
                case '"':  escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n";  break;
                case '\t': escaped += "\\t";  break;
                case '\r': escaped += "\\r";  break;
                default:   escaped += c;      break;
            }
        }
        return "granda_str_literal(\"" + escaped + "\")";
    }

    case Expr::Kind::BoolLit:
        return expr.bool_val ? "1" : "0";

    case Expr::Kind::NullLit:
        return "NULL";

    case Expr::Kind::Ident:
        return expr.str_val;

    case Expr::Kind::SelfExpr:
        return "self";

    case Expr::Kind::Binary: {
        std::string lv = emit_expr(*expr.left);
        std::string rv = emit_expr(*expr.right);
        /* String concatenation */
        if (expr.bin_op == BinOp::ADD &&
            expr.left->resolved_type.kind == TypeKind::STR) {
            return "granda_str_concat(" + lv + ", " + rv + ")";
        }
        /* String equality */
        if ((expr.bin_op == BinOp::EQ || expr.bin_op == BinOp::NEQ) &&
            expr.left->resolved_type.kind == TypeKind::STR) {
            std::string eq = "granda_str_eq(" + lv + ", " + rv + ")";
            return (expr.bin_op == BinOp::NEQ) ? "!" + eq : eq;
        }
        return "(" + lv + " " + bin_op_str(expr.bin_op, expr.resolved_type)
               + " " + rv + ")";
    }

    case Expr::Kind::Unary: {
        std::string ov = emit_expr(*expr.operand);
        if (expr.un_op == UnOp::NEG) return "(-" + ov + ")";
        return "(!" + ov + ")";
    }

    case Expr::Kind::Call: {
        /* Built-in functions */
        const std::string& fn = expr.callee_name;
        if (fn == "print" || fn == "println") {
            std::string arg = emit_expr(*expr.args[0]);
            const TypeRef& at = expr.args[0]->resolved_type;
            std::string tmp = fresh_tmp();
            if (at.kind == TypeKind::STR) {
                iout(indent_str(indent) + (fn == "println"
                    ? "granda_println(" + arg + ");"
                    : "granda_print("   + arg + ");"));
            } else if (at.kind == TypeKind::INT) {
                iout("granda_print_int(" + arg + ");");
                if (fn == "println") iout("putchar('\\n');");
            } else if (at.kind == TypeKind::FLOAT) {
                iout("granda_print_float(" + arg + ");");
                if (fn == "println") iout("putchar('\\n');");
            } else if (at.kind == TypeKind::BOOL) {
                iout("granda_print_bool(" + arg + ");");
                if (fn == "println") iout("putchar('\\n');");
            } else {
                iout("granda_println(granda_str_literal(\"<object>\"));");
            }
            (void)tmp;
            return "";
        }
        if (fn == "len") {
            std::string arg = emit_expr(*expr.args[0]);
            const TypeRef& at = expr.args[0]->resolved_type;
            if (at.kind == TypeKind::STR)   return "granda_str_len(" + arg + ")";
            return "granda_array_len(" + arg + ")";
        }
        if (fn == "str") {
            std::string arg = emit_expr(*expr.args[0]);
            const TypeRef& at = expr.args[0]->resolved_type;
            if (at.kind == TypeKind::INT)   return "granda_int_to_str(" + arg + ")";
            if (at.kind == TypeKind::FLOAT) return "granda_float_to_str(" + arg + ")";
            if (at.kind == TypeKind::BOOL)  return "granda_bool_to_str(" + arg + ")";
            return arg; /* already str */
        }
        if (fn == "int") {
            std::string arg = emit_expr(*expr.args[0]);
            return "(int64_t)(" + arg + ")";
        }
        if (fn == "float") {
            std::string arg = emit_expr(*expr.args[0]);
            return "(double)(" + arg + ")";
        }
        if (fn == "assert") {
            std::string arg = emit_expr(*expr.args[0]);
            iout("if (!(" + arg + ")) granda_panic(\"Assertion failed\");");
            return "";
        }
        if (fn == "push") {
            std::string arr = emit_expr(*expr.args[0]);
            std::string val = emit_expr(*expr.args[1]);
            const TypeRef& et = expr.args[1]->resolved_type;
            if (et.kind == TypeKind::INT)
                iout("granda_int_array_push(" + arr + ", " + val + ");");
            else if (et.kind == TypeKind::FLOAT)
                iout("granda_float_array_push(" + arr + ", " + val + ");");
            else
                iout("granda_array_push(" + arr + ", " + val + ");");
            return "";
        }
        if (fn == "pop") {
            std::string arr = emit_expr(*expr.args[0]);
            const TypeRef& t = expr.args[0]->resolved_type;
            if (t.kind == TypeKind::ARRAY && t.elem) {
                if (t.elem->kind == TypeKind::INT) return "granda_int_array_pop(" + arr + ")";
                if (t.elem->kind == TypeKind::FLOAT) return "granda_float_array_pop(" + arr + ")";
            }
            return "granda_array_pop(" + arr + ")";
        }

        /* ---- Math ---- */
        if (fn == "sin") return "granda_math_sin(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "cos") return "granda_math_cos(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "tan") return "granda_math_tan(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "sqrt") return "granda_math_sqrt(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "pow") return "granda_math_pow(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "abs") {
            const TypeRef& t = expr.args[0]->resolved_type;
            if (t.kind == TypeKind::FLOAT) return "granda_math_abs_f(" + emit_expr(*expr.args[0]) + ")";
            return "granda_math_abs_i(" + emit_expr(*expr.args[0]) + ")";
        }
        if (fn == "floor") return "granda_math_floor(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "ceil") return "granda_math_ceil(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "round") return "granda_math_round(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "min") {
            const TypeRef& t = expr.args[0]->resolved_type;
            if (t.kind == TypeKind::FLOAT) return "granda_math_min_f(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
            return "granda_math_min_i(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        }
        if (fn == "max") {
            const TypeRef& t = expr.args[0]->resolved_type;
            if (t.kind == TypeKind::FLOAT) return "granda_math_max_f(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
            return "granda_math_max_i(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        }
        if (fn == "log") return "granda_math_log(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "log2") return "granda_math_log2(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "log10") return "granda_math_log10(" + emit_expr(*expr.args[0]) + ")";

        /* ---- String stdlib ---- */
        if (fn == "substr") return "granda_str_substr(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ", " + emit_expr(*expr.args[2]) + ")";
        if (fn == "index_of") return "granda_str_index_of(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "contains") return "granda_str_contains(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "to_upper") return "granda_str_to_upper(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "to_lower") return "granda_str_to_lower(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "trim") return "granda_str_trim(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "split") return "granda_str_split(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "replace") return "granda_str_replace(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ", " + emit_expr(*expr.args[2]) + ")";
        if (fn == "starts_with") return "granda_str_starts_with(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "ends_with") return "granda_str_ends_with(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "char_at") return "granda_str_char_at(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "str_to_int") return "granda_str_to_int(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "str_to_float") return "granda_str_to_float(" + emit_expr(*expr.args[0]) + ")";

        /* ---- Array stdlib ---- */
        if (fn == "sort") {
            const TypeRef& t = expr.args[0]->resolved_type;
            if (t.kind == TypeKind::ARRAY && t.elem) {
                if (t.elem->kind == TypeKind::INT) return "granda_array_sort_i(" + emit_expr(*expr.args[0]) + ")";
                if (t.elem->kind == TypeKind::FLOAT) return "granda_array_sort_f(" + emit_expr(*expr.args[0]) + ")";
            }
            return "granda_array_sort_i(" + emit_expr(*expr.args[0]) + ")";
        }
        if (fn == "reverse") return "granda_array_reverse(" + emit_expr(*expr.args[0]) + ")";

        /* ---- File I/O ---- */
        if (fn == "read_file") return "granda_read_file(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "write_file") { iout("granda_write_file(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ");"); return ""; }
        if (fn == "file_exists") return "granda_file_exists(" + emit_expr(*expr.args[0]) + ")";
        if (fn == "read_line") return "granda_read_line()";
        if (fn == "args") return "granda_args()";

        /* ---- Random ---- */
        if (fn == "rand_int") return "granda_rand_int(" + emit_expr(*expr.args[0]) + ", " + emit_expr(*expr.args[1]) + ")";
        if (fn == "rand_float") return "granda_rand_float()";
        if (fn == "rand_seed") { iout("granda_rand_seed(" + emit_expr(*expr.args[0]) + ");"); return ""; }

        /* ---- Time ---- */
        if (fn == "time_now") return "granda_time_now()";
        if (fn == "time_sleep") { iout("granda_time_sleep(" + emit_expr(*expr.args[0]) + ");"); return ""; }

        /* ---- OS ---- */
        if (fn == "os_exit") { iout("granda_os_exit(" + emit_expr(*expr.args[0]) + ");"); return ""; }
        if (fn == "os_env") return "granda_os_env(" + emit_expr(*expr.args[0]) + ")";

        /* User-defined function */
        auto fit = fn_decls.find(fn);
        std::string call = fn + "(";
        for (size_t i = 0; i < expr.args.size(); ++i) {
            if (i) call += ", ";
            std::string arg_val = emit_expr(*expr.args[i]);
            /* Check if the corresponding param is trait-typed */
            if (fit != fn_decls.end() && i < fit->second->params.size()) {
                const TypeRef& param_type = fit->second->params[i].type;
                if (param_type.kind == TypeKind::TRAIT) {
                    /* Find the concrete class implementing this trait */
                    const TypeRef& arg_type = expr.args[i]->resolved_type;
                    std::string concrete_cls = arg_type.class_name;
                    if (!concrete_cls.empty() && class_implements_trait(concrete_cls, param_type.class_name)) {
                        arg_val = "&" + concrete_cls + "_" + param_type.class_name
                            + "_vtable, (void*)" + arg_val;
                    } else {
                        /* Unknown concrete type — just cast to void* */
                        arg_val = "NULL, (void*)" + arg_val;
                    }
                } else {
                    /* Subtyping cast: if argument is a subclass of the parameter, cast it */
                    const TypeRef& arg_type = expr.args[i]->resolved_type;
                    if (arg_type.kind == TypeKind::CLASS && param_type.kind == TypeKind::CLASS
                        && arg_type.class_name != param_type.class_name
                        && is_subclass(arg_type.class_name, param_type.class_name)) {
                        arg_val = "(" + param_type.class_name + "*)" + arg_val;
                    }
                }
            }
            call += arg_val;
        }
        call += ")";
        return call;
    }

    case Expr::Kind::MethodCall: {
        std::string obj = emit_expr(*expr.object);
        std::string cls_name = expr.object->resolved_type.class_name;
        TypeKind obj_kind = expr.object->resolved_type.kind;

        /* Trait method dispatch: use hidden vtable parameter */
        if (obj_kind == TypeKind::TRAIT) {
            /* Look up by variable name (not trait name) */
            std::string var_name = (expr.object->kind == Expr::Kind::Ident)
                ? expr.object->str_val : "";
            auto vpit = trait_param_to_vtable.find(var_name);
            if (vpit != trait_param_to_vtable.end()) {
                /* obj is a trait-typed parameter — use the hidden vtable param */
                std::string vt_param = vpit->second;
                std::string dispatch = vt_param + "->" + expr.method_name + "(" + obj;
                for (auto& a : expr.args)
                    dispatch += ", " + emit_expr(*a);
                dispatch += ")";
                return dispatch;
            }
            /* Trait default calling other trait methods on self */
            if (!current_trait.empty() && var_name == "self") {
                std::string call = mangle(current_trait, expr.method_name)
                    + "(self";
                for (auto& a : expr.args)
                    call += ", " + emit_expr(*a);
                call += ")";
                return call;
            }
            /* Fallback: non-param trait variable — use direct cast */
            std::string dispatch = "((" + cls_name + "_Vtable*)" + obj
                + "->_vtable_" + cls_name + ")->" + expr.method_name
                + "((void*)" + obj;
            for (auto& a : expr.args)
                dispatch += ", " + emit_expr(*a);
            dispatch += ")";
            return dispatch;
        }

        /* Check if this method is virtual and needs vtable dispatch */
        bool is_virtual = false;
        std::string vt_root = get_vtable_root(cls_name);
        if (!vt_root.empty()) {
            /* Check if the method is in the vtable */
            auto mlit = vtable_method_list.find(vt_root);
            if (mlit != vtable_method_list.end()) {
                for (auto& mname : mlit->second)
                    if (mname == expr.method_name) { is_virtual = true; break; }
            }
        }

        if (is_virtual) {
            /* Vtable dispatch: ((RootVtable*)obj->_vtable)->method((void*)obj, args...) */
            std::string dispatch = "((" + vt_root + "_Vtable*)" + obj + "->_vtable)->"
                + expr.method_name + "((void*)" + obj;
            for (auto& a : expr.args)
                dispatch += ", " + emit_expr(*a);
            dispatch += ")";
            return dispatch;
        }

        /* Non-virtual: static dispatch — use the defining class for correct mangled name */
        std::string defining_cls = find_defining_class(cls_name, expr.method_name);
        std::string self_arg = obj;
        if (defining_cls != cls_name) {
            self_arg = "(" + defining_cls + "*)" + obj;
        }
        std::string call = mangle(defining_cls, expr.method_name) + "(" + self_arg;
        for (auto& a : expr.args)
            call += ", " + emit_expr(*a);
        call += ")";
        return call;
    }

    case Expr::Kind::StaticCall: {
        std::string mangled_name;
        if (modules.count(expr.class_name)) {
            mangled_name = expr.class_name + "::" + expr.method_name;
            for (auto& c : mangled_name) if (c == ':') c = '_';
        } else {
            mangled_name = mangle(expr.class_name, expr.method_name);
        }
        std::string call = mangled_name + "(";
        for (size_t i = 0; i < expr.args.size(); ++i) {
            if (i) call += ", ";
            call += emit_expr(*expr.args[i]);
        }
        call += ")";
        return call;
    }

    case Expr::Kind::FieldAccess: {
        std::string obj = emit_expr(*expr.object);
        return obj + "->" + expr.str_val;
    }

    case Expr::Kind::Index: {
        std::string arr = emit_expr(*expr.left);
        std::string idx = emit_expr(*expr.index);
        const TypeRef& et = expr.resolved_type;
        if (et.kind == TypeKind::INT)
            return "granda_int_array_get(" + arr + ", " + idx + ")";
        if (et.kind == TypeKind::FLOAT)
            return "granda_float_array_get(" + arr + ", " + idx + ")";
        return "(" + c_type(et) + ")granda_array_get(" + arr + ", " + idx + ")";
    }

    case Expr::Kind::Assign: {
        std::string rhs = emit_expr(*expr.right);
        if (expr.left->kind == Expr::Kind::Ident) {
            std::string lhs = expr.left->str_val;
            if (type_is_gc(expr.left->resolved_type))
                return "RC_ASSIGN(" + lhs + ", " + rhs + "), " + lhs;
            return "(" + lhs + " = " + rhs + ")";
        }
        if (expr.left->kind == Expr::Kind::FieldAccess) {
            std::string obj = emit_expr(*expr.left->object);
            std::string fld = expr.left->str_val;
            if (type_is_gc(expr.resolved_type))
                return "RC_ASSIGN(" + obj + "->" + fld + ", " + rhs + "), "
                       + obj + "->" + fld;
            return "(" + obj + "->" + fld + " = " + rhs + ")";
        }
        if (expr.left->kind == Expr::Kind::Index) {
            std::string arr = emit_expr(*expr.left->left);
            std::string idx = emit_expr(*expr.left->index);
            const TypeRef& et = expr.resolved_type;
            if (et.kind == TypeKind::INT)
                return "granda_int_array_set(" + arr + ", " + idx + ", " + rhs + "), " + rhs;
            if (et.kind == TypeKind::FLOAT)
                return "granda_float_array_set(" + arr + ", " + idx + ", " + rhs + "), " + rhs;
            return "granda_array_set(" + arr + ", " + idx + ", " + rhs + "), " + rhs;
        }
        return "(/* assign */ " + rhs + ")";
    }

    case Expr::Kind::CompAssign: {
        std::string rhs = emit_expr(*expr.right);
        if (expr.left->kind == Expr::Kind::Ident) {
            std::string lhs = expr.left->str_val;
            std::string op  = bin_op_str(expr.comp_op, expr.resolved_type);
            return "(" + lhs + " " + op + "= " + rhs + ")";
        }
        return "0";
    }

    case Expr::Kind::ArrayLit: {
        if (expr.elements.empty())
            return "granda_array_new(0, 0)";

        TypeRef inferred_elem(TypeKind::INFERRED);
        const TypeRef& et = expr.resolved_type.elem
            ? *expr.resolved_type.elem : inferred_elem;

        std::string tmp = fresh_tmp();
        std::string ct  = c_type(TypeRef::array_of(et.clone()));
        int is_gc = type_is_gc(et) ? 1 : 0;

        if (et.kind == TypeKind::INT) {
            iout(ct + " " + tmp + " = granda_int_array_new("
                 + std::to_string(expr.elements.size()) + ");");
            for (auto& elem : expr.elements) {
                std::string ev = emit_expr(*elem);
                iout("granda_int_array_push(" + tmp + ", " + ev + ");");
            }
        } else if (et.kind == TypeKind::FLOAT) {
            iout(ct + " " + tmp + " = granda_float_array_new("
                 + std::to_string(expr.elements.size()) + ");");
            for (auto& elem : expr.elements) {
                std::string ev = emit_expr(*elem);
                iout("granda_float_array_push(" + tmp + ", " + ev + ");");
            }
        } else {
            iout(ct + " " + tmp + " = granda_array_new("
                 + std::to_string(expr.elements.size()) + ", "
                 + std::to_string(is_gc) + ");");
            for (auto& elem : expr.elements) {
                std::string ev = emit_expr(*elem);
                iout("granda_array_push(" + tmp + ", (void*)" + ev + ");");
            }
        }
        return tmp;
    }

    case Expr::Kind::ClassCons: {
        std::string tmp = fresh_tmp();
        iout(expr.class_name + "* " + tmp + " = " + expr.class_name + "_alloc();");
        for (auto& fi : expr.field_inits) {
            std::string val = emit_expr(*fi.value);
            if (type_is_gc(fi.value->resolved_type))
                iout("RC_ASSIGN(" + tmp + "->" + fi.name + ", " + val + ");");
            else
                iout(tmp + "->" + fi.name + " = " + val + ";");
        }
        return tmp;
    }

    case Expr::Kind::Range:
        /* Range is only consumed by for-loops; if used elsewhere return 0 */
        return "0";
    }

    return "0";
}
