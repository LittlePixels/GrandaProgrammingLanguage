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
        << "Granda compiler 0.1.0\n\n"
        << "Usage:\n"
        << "  " << prog << " <source.gra> [options]\n\n"
        << "Options:\n"
        << "  -o <output>    Output binary name (default: a.out)\n"
        << "  --emit-c       Write generated C to stdout and exit\n"
        << "  --cc <cmd>     C compiler to use (default: cc)\n"
        << "  --runtime <dir> Directory containing granda_rt.h and granda_rt.c\n"
        << "  -h / --help    Show this message\n";
}

static std::string find_runtime(const std::string& exe_dir) {
    /* Look next to the compiler binary first */
    if (fs::exists(exe_dir + "/granda_rt.h")) return exe_dir;
    /* Then the runtime/ subdirectory in the source tree */
    if (fs::exists(exe_dir + "/../runtime/granda_rt.h"))
        return fs::canonical(exe_dir + "/../runtime").string();
    /* Installed share dir */
    if (fs::exists(exe_dir + "/../share/granda/runtime/granda_rt.h"))
        return fs::canonical(exe_dir + "/../share/granda/runtime").string();
    return "";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string source_file;
    std::string output_file = "a.out";
    std::string cc          = "cc";
    std::string runtime_dir;
    bool emit_c = false;

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

    /* --- Lex --- */
    std::string src;
    try { src = read_file(source_file); }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::vector<Token> tokens;
    try {
        Lexer lexer(src, source_file);
        tokens = lexer.tokenize();
    } catch (const LexError& e) {
        std::cerr << source_file << ":" << e.line << ":" << e.col
                  << ": lexer error: " << e.what() << '\n';
        return 1;
    }

    /* --- Parse --- */
    Program prog;
    try {
        Parser parser(std::move(tokens));
        prog = parser.parse();
    } catch (const ParseError& e) {
        std::cerr << source_file << ":" << e.line << ":" << e.col
                  << ": parse error: " << e.what() << '\n';
        return 1;
    }

    /* --- Type check --- */
    try {
        TypeChecker tc;
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
