#include "lesl/arena.hpp"
#include "lesl/lesl.hpp"
#include "lesl/sdl.hpp"

#include <fstream>
#include <istream>
#include <optional>
#include <spirv_binary_container.hpp>

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
    std::optional<std::string> pipeline;
};

static std::vector<std::string> split(std::string s, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        tokens.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    tokens.push_back(s);

    return tokens;
}

static Args parse_args(int argc, char* argv[]) {
    Args args;

    if (argc < 2) {
        return args; // read from stdin, write to stdout
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [input] [-p,--pipeline PIPELINE]"
                      << std::endl;
            exit(0);
        } else if (arg == "--pipeline" || arg == "-p") {
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

    if (!args.input.has_value()) {
        return 1;
    }

    if (!args.pipeline.has_value()) {
        return 1;
    }

    size_t size;
    void* data = SDL_LoadFile(args.input.value().c_str(), &size);

    if (!data) {
        return 1;
    }

    std::ostream* out = new std::ofstream("/dev/null", std::ios::binary);

    if (!out->good()) {
        std::cerr << "error: failed to open output file" << std::endl;
        return 1;
    }

    lesl::CompilationArena arena{};

    auto t1 = std::chrono::high_resolution_clock::now();
    for (uint32_t step = 0; step < 10000; step++) {
        lesl::sdl::SDL3BindingManager binding_manager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
        };
        lesl::compile(
            (const char*)data,
            args.pipeline->c_str(),
            std::move(binding_manager),
            &arena
        );
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1) << std::endl;

    delete out;

    return 0;
}
