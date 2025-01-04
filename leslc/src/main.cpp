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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

struct Args {
    std::optional<std::string> input;
    std::optional<std::string> output;
};

Args parse_args(int argc, char* argv[]) {
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

    std::ostream* out =
        args.output.has_value() ? new std::ofstream(args.output.value(), std::ios::binary) : &std::cout;

    if (!out->good()) {
        std::cerr << "error: failed to open output file" << std::endl;
        return 1;
    }

#ifdef _WIN32
    // switch to binary mode on Windows
    if (!args.input.has_value()) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    // if (!args.output.has_value()) {
    //     _setmode(_fileno(stdout), _O_BINARY);
    // }
#endif

    StringPool pool(0x1000);
    ErrorHandler error_handler;

    Unit unit(*in);

    Tokenizer tokenizer(pool, unit, error_handler);

    Parser parser(tokenizer, error_handler);

    Module module = parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    if (args.input.has_value()) {
        delete in;
    }

    if (args.output.has_value()) {
        delete out;
    }

    return 0;
}
