#include "codegen.hpp"
#include "error_handler.hpp"
#include "parser.hpp"
#include "repr.hpp"
#include "stringpool.hpp"
#include "tokenizer.hpp"
#include "unit.hpp"

#include <fstream>
#include <istream>
#include <optional>
#include <spirv_binary_container.hpp>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <ostream>
#include <string>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

struct Args {
    std::optional<std::string> input;
    std::optional<std::string> output;
};

static Args parse_args(int argc, char* argv[]) {
    Args args;

    if (argc < 2) {
        return args; // read from stdin, write to stdout
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [-o output] [input]" << std::endl;
            exit(0);
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                args.output = argv[i + 1];
                i++;
            }

        } else {
            if (args.input.has_value()) {
                std::cerr << "error: multiple input files specified" << std::endl;
                exit(1);
            }
            args.input = arg;
        }
    }

    return args;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    std::istream* in =
        args.input.has_value() ? new std::ifstream(args.input.value()) : &std::cin;

    if (!in->good()) {
        std::cerr << "error: failed to open input file" << std::endl;
        return 1;
    }

    std::ostream* out = args.output.has_value()
                            ? new std::ofstream(args.output.value(), std::ios::binary)
                            : &std::cout;

    if (!out->good()) {
        std::cerr << "error: failed to open output file" << std::endl;
        return 1;
    }

    CompilationArena arena{};

    ErrorHandler error_handler;

    Unit unit(*in);

    Tokenizer tokenizer(arena, unit, error_handler);

    Parser parser(arena, tokenizer, error_handler);

    Module module = parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    ReprPrinter printer{ *out };

    for (const auto& decl : module.decls) {
        printer.print(*decl);
    }

    arena.clear();

    if (args.input.has_value()) {
        delete in;
    }

    if (args.output.has_value()) {
        delete out;
    }

    return 0;
}
