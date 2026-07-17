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
#ifdef _WIN32
#include <io.h>
#include <process.h>
#endif

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
        << "  -o <output>      Output binary name (default: a.exe / a.out)\n"
        << "  --emit-c         Write generated C to stdout and exit\n"
        << "  --cc <cmd>       C compiler to use (auto-detected if not set)\n"
        << "  --runtime <dir>  Directory containing granda_rt.h and granda_rt.c\n"
        << "  -I <dir>         Add include path for module resolution\n"
        << "  -h / --help      Show this message\n";
}

/* Try to find a C compiler on the system */
static std::string find_cc() {
#ifdef _WIN32
    const char* candidates[] = {
        "C:\\msys64\\ucrt64\\bin\\gcc.exe",
        "C:\\msys64\\mingw64\\bin\\gcc.exe",
        "C:\\msys64\\usr\\bin\\gcc.exe",
        "C:\\MinGW\\bin\\gcc.exe",
        "C:\\msys64\\ucrt64\\bin\\clang.exe",
    };
    for (auto c : candidates) {
        if (_access(c, 0) == 0) return std::string(c);
    }
    return "gcc";
#else
    if (std::system("gcc --version >/dev/null 2>&1") == 0) return "gcc";
    if (std::system("cc --version >/dev/null 2>&1") == 0) return "cc";
    return "cc";
#endif
}

/* Search for granda_rt.h/.c relative to the given directory */
static std::string find_runtime(const std::string& exe_dir) {
    /* Check in the exe's own directory first */
    if (fs::exists(fs::path(exe_dir) / "granda_rt.h"))
        return exe_dir;
    /* Check ./runtime/ (bundled alongside exe) */
    fs::path rt0 = fs::path(exe_dir) / "runtime";
    if (fs::exists(rt0 / "granda_rt.h"))
        return fs::weakly_canonical(rt0).string();
    /* Check ../runtime/ (project layout) */
    fs::path rt1 = fs::path(exe_dir) / ".." / "runtime";
    if (fs::exists(rt1 / "granda_rt.h"))
        return fs::weakly_canonical(rt1).string();
    /* Check ../../runtime/ (if exe is in build/) */
    fs::path rt3 = fs::path(exe_dir) / ".." / ".." / "runtime";
    if (fs::exists(rt3 / "granda_rt.h"))
        return fs::weakly_canonical(rt3).string();
    /* Check ../share/granda/runtime/ (install layout) */
    fs::path rt2 = fs::path(exe_dir) / ".." / "share" / "granda" / "runtime";
    if (fs::exists(rt2 / "granda_rt.h"))
        return fs::weakly_canonical(rt2).string();
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
#ifdef _WIN32
    std::string output_file = "a.exe";
#else
    std::string output_file = "a.out";
#endif
    std::string cc;          /* empty = auto-detect */
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

    /* --- Auto-detect C compiler --- */
    if (cc.empty()) cc = find_cc();

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
    std::string tmp_c = fs::temp_directory_path().string() + "\\granda_out.c";
    {
        std::ofstream f(tmp_c);
        if (!f) {
            std::cerr << "Error: cannot write to temp file " << tmp_c << '\n';
            return 1;
        }
        f << c_code;
    }

    /* --- Invoke C compiler --- */
    std::string rt_fwd = runtime_dir;
    for (auto& ch : rt_fwd) { if (ch == '\\') ch = '/'; }
    std::string tmp_fwd = tmp_c;
    for (auto& ch : tmp_fwd) { if (ch == '\\') ch = '/'; }
    std::string rt_c_file = rt_fwd + "/granda_rt.c";

    const char* cc_cstr = cc.c_str();
#ifdef _WIN32
    const char* args[] = {
        cc_cstr,
        tmp_fwd.c_str(),
        rt_c_file.c_str(),
        "-I", rt_fwd.c_str(),
        "-o", output_file.c_str(),
        "-O2", "-lm", nullptr
    };
    int ret = _spawnvp(_P_WAIT, cc_cstr, args);
    if (ret != 0) {
        std::cerr << "C compiler failed (exit " << ret << ")\n";
        std::cerr << "Command: " << cc << " " << tmp_fwd
                  << " " << rt_c_file
                  << " -I " << rt_fwd
                  << " -o " << output_file
                  << " -O2 -lm\n";
        std::cerr << "Generated C written to: " << tmp_c << '\n';
        return 1;
    }
#else
    std::string cmd = "\""
        + cc + "\" \""
        + tmp_fwd + "\" \""
        + rt_fwd + "/granda_rt.c\""
        + " -I \"" + rt_fwd + "\""
        + " -o \"" + output_file + "\""
        + " -O2 -lm";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "C compiler failed (exit " << ret << ")\n";
        std::cerr << "Command: " << cmd << '\n';
        std::cerr << "Generated C written to: " << tmp_c << '\n';
        return 1;
    }
#endif

    return 0;
}
