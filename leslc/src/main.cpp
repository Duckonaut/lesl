#include "arena.hpp"
#include "colorize.hpp"
#include "unit.hpp"
#include "error_handler.hpp"
#include "tokenizer.hpp"
#include "repr.hpp"
#include "parser.hpp"
#include "validator.hpp"
#include "codegen.hpp"

#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <spirv_binary_container.hpp>

#include <cstdio>
#include <iostream>
#include <ostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

struct Args {
    std::optional<std::string> input;
    std::optional<std::string> output;
    std::optional<std::string> pipeline;
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
        } else if (arg == "--pipeline") {
            if (i + 1 < argc) {
                args.pipeline = argv[i + 1];
                i++;
            } else {
                std::cerr << "error: --pipeline requires an argument" << std::endl;
                exit(1);
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

    parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    ReprPrinter printer{ std::cout };

    for (auto decl : arena.decls) {
        printer.print(*decl);
    }

    Validator validator(arena, error_handler);

    validator.validate();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    for (auto typeinfo : arena.types) {
        std::cout << colorize::bright_green("Type") << " " << typeinfo->name.c_str() << std::endl;
    }

    BindingManager binding_manager(
        BindingManager::TargetAPI::SDL3,
        BindingManager::BindingAllocationMode::SingleInputMultipleUniform
    );

    CodeGenerator codegen(arena, binding_manager, args.pipeline);

    codegen.generate();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    codegen.flush(*out);

    arena.clear();

    if (args.input.has_value()) {
        delete in;
    }

    if (args.output.has_value()) {
        delete out;
    }

    return 0;
}
