#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "codegen.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void usage(const char* prog) {
    std::cerr
        << "Granda compiler 0.2.0\n\n"
        << "Usage:\n"
        << "  " << prog << " <source.gra> [options]\n\n"
        << "Options:\n"
        << "  -o <output>      Output binary name (default: a.out)\n"
        << "  --emit-c         Write generated C to stdout and exit\n"
        << "  --cc <cmd>       C compiler to use (default: cc)\n"
        << "  --runtime <dir>  Directory containing granda_rt.h and granda_rt.c\n"
        << "  -I <dir>         Add include path for module resolution\n"
        << "  -h / --help      Show this message\n";
}

static std::string find_runtime(const std::string& exe_dir) {
    if (fs::exists(exe_dir + "/granda_rt.h")) return exe_dir;
    if (fs::exists(exe_dir + "/../runtime/granda_rt.h"))
        return fs::canonical(exe_dir + "/../runtime").string();
    if (fs::exists(exe_dir + "/../share/granda/runtime/granda_rt.h"))
        return fs::canonical(exe_dir + "/../share/granda/runtime").string();
    return "";
}

/* -----------------------------------------------------------------------
 * Module resolution
 * --------------------------------------------------------------------- */
static std::string resolve_import_path(const std::string& import_path,
                                       const std::vector<std::string>& include_paths,
                                       const std::string& source_dir) {
    std::string file_part = import_path;
    for (auto& c : file_part) if (c == ':') c = '/';
    file_part += ".gra";

    /* Search in source file's directory first */
    if (!source_dir.empty()) {
        std::string full = source_dir + "/" + file_part;
        if (fs::exists(full)) return fs::canonical(full).string();
    }
    /* Search in include paths */
    for (auto& dir : include_paths) {
        std::string full = dir + "/" + file_part;
        if (fs::exists(full)) return fs::canonical(full).string();
    }
    /* Search in current directory */
    if (fs::exists(file_part)) return fs::canonical(file_part).string();

    return "";
}

static Program parse_file(const std::string& file_path) {
    std::string src = read_file(file_path);
    Lexer lexer(src, file_path);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parse();
}

static void load_imports(Program& prog,
                          const std::vector<std::string>& include_paths,
                          const std::string& source_dir,
                          std::unordered_set<std::string>& loaded,
                          std::unordered_map<std::string, Program>& modules,
                          std::unordered_set<std::string>& module_names) {
    for (auto& imp : prog.imports) {
        if (loaded.count(imp.path)) continue;

        std::string file_path = resolve_import_path(imp.path, include_paths, source_dir);
        if (file_path.empty()) {
            throw std::runtime_error("Cannot find module '" + imp.path + "'"
                                     " (imported from " + source_dir + ")");
        }

        loaded.insert(imp.path);
        std::string mod_dir = fs::path(file_path).parent_path().string();
        Program mod_prog = parse_file(file_path);

        /* Recursively load imports from the module */
        load_imports(mod_prog, include_paths, mod_dir, loaded, modules, module_names);

        /* Store module program keyed by import path */
        modules[imp.path] = std::move(mod_prog);
        module_names.insert(imp.path);
    }
}

static void merge_module(Program& main_prog, Program& mod_prog,
                          const std::string& mod_name) {
    auto prefix = [&](const std::string& name) -> std::string {
        return mod_name + "::" + name;
    };

    for (auto& trait : mod_prog.traits)
        trait.name = prefix(trait.name);
    for (auto& t : mod_prog.traits)
        main_prog.traits.push_back(std::move(t));

    for (auto& cls : mod_prog.classes) {
        cls.name = prefix(cls.name);
        if (!cls.base_class.empty()) cls.base_class = prefix(cls.base_class);
        for (auto& impl : cls.implements) impl = prefix(impl);
    }
    for (auto& c : mod_prog.classes)
        main_prog.classes.push_back(std::move(c));

    for (auto& fn : mod_prog.functions)
        fn.name = prefix(fn.name);
    for (auto& f : mod_prog.functions)
        main_prog.functions.push_back(std::move(f));
}

/* -----------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------- */
int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string source_file;
    std::string output_file = "a.out";
    std::string cc          = "cc";
    std::string runtime_dir;
    bool emit_c = false;
    std::vector<std::string> include_paths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        if (arg == "--emit-c")  { emit_c = true; }
        else if (arg == "-o") {
            if (++i >= argc) { std::cerr << "Expected output name after -o\n"; return 1; }
            output_file = argv[i];
        } else if (arg == "--cc") {
            if (++i >= argc) { std::cerr << "Expected compiler after --cc\n"; return 1; }
            cc = argv[i];
        } else if (arg == "--runtime") {
            if (++i >= argc) { std::cerr << "Expected directory after --runtime\n"; return 1; }
            runtime_dir = argv[i];
        } else if (arg == "-I") {
            if (++i >= argc) { std::cerr << "Expected directory after -I\n"; return 1; }
            include_paths.push_back(argv[i]);
        } else if (arg[0] != '-') {
            source_file = arg;
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            return 1;
        }
    }

    if (source_file.empty()) {
        std::cerr << "Error: no source file given\n";
        usage(argv[0]); return 1;
    }

    /* --- Parse main file --- */
    std::string source_dir = fs::path(source_file).parent_path().string();
    Program prog;
    try { prog = parse_file(source_file); }
    catch (const LexError& e) {
        std::cerr << source_file << ":" << e.line << ":" << e.col
                  << ": lexer error: " << e.what() << '\n';
        return 1;
    } catch (const ParseError& e) {
        std::cerr << source_file << ":" << e.line << ":" << e.col
                  << ": parse error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    /* --- Load and merge modules --- */
    std::unordered_set<std::string> loaded;
    std::unordered_map<std::string, Program> modules;
    std::unordered_set<std::string> module_names;
    try {
        load_imports(prog, include_paths, source_dir, loaded, modules, module_names);
        /* Merge all modules into the main program */
        for (auto& [mod_name, mod_prog] : modules) {
            merge_module(prog, mod_prog, mod_name);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    /* --- Type check --- */
    try {
        TypeChecker tc;
        tc.set_modules(module_names);
        tc.check(prog);
    } catch (const TypeError& e) {
        std::cerr << source_file << ":" << e.line
                  << ": type error: " << e.what() << '\n';
        return 1;
    }

    /* --- Codegen --- */
    std::string c_code;
    try {
        Codegen cg;
        cg.set_modules(module_names);
        c_code = cg.emit(prog);
    } catch (const CodegenError& e) {
        std::cerr << source_file << ":" << e.line
                  << ": codegen error: " << e.what() << '\n';
        return 1;
    }

    if (emit_c) {
        std::cout << c_code;
        return 0;
    }

    /* --- Find runtime --- */
    if (runtime_dir.empty()) {
        fs::path exe_path = fs::weakly_canonical(argv[0]);
        runtime_dir = find_runtime(exe_path.parent_path().string());
    }
    if (runtime_dir.empty()) {
        std::cerr << "Error: cannot find granda_rt.h / granda_rt.c\n"
                  << "Use --runtime <dir> to specify their location\n";
        return 1;
    }

    /* --- Write generated C to a temp file --- */
    std::string tmp_c = fs::temp_directory_path().string() + "/granda_out.c";
    {
        std::ofstream f(tmp_c);
        if (!f) {
            std::cerr << "Error: cannot write to temp file " << tmp_c << '\n';
            return 1;
        }
        f << c_code;
    }

    /* --- Invoke C compiler --- */
    std::string cmd = cc
        + " \"" + tmp_c + "\""
        + " \"" + runtime_dir + "/granda_rt.c\""
        + " -I \"" + runtime_dir + "\""
        + " -o \"" + output_file + "\""
        + " -O2 -lm 2>&1";

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "C compiler failed (exit " << ret << ")\n";
        std::cerr << "Generated C written to: " << tmp_c << '\n';
        return 1;
    }

    return 0;
}
