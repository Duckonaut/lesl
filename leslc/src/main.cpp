#include "lesl/arena.hpp"
#include "lesl/binding_manager.hpp"
#include "lesl/colorize.hpp"
#include "lesl/sdl.hpp"
#include "lesl/unit.hpp"
#include "lesl/error_handler.hpp"
#include "lesl/tokenizer.hpp"
#include "lesl/repr.hpp"
#include "lesl/parser.hpp"
#include "lesl/validator.hpp"
#include "lesl/codegen.hpp"

#include <fstream>
#include <istream>
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

enum class BindingManagerType {
    SDL3,
    Simple,
    Dictionary,
};

struct Args {
    std::optional<std::string> input;
    std::optional<std::string> output;
    std::optional<std::string> pipeline;
    std::optional<BindingManagerType> binding_manager;
    std::vector<lesl::DictionaryBindingManager::InterfaceBinding> dict_binds;
    bool verbose = false;
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

static std::optional<lesl::DictionaryBindingManager::InterfaceBinding>
parse_bind(std::string b) {
    // format NAME:STAGE:STORAGE_CLASS:SET:SLOT

    std::vector<std::string> parts = split(b, ":");

    if (parts.size() != 5)
        return std::nullopt;

    std::string name = parts[0];

    std::string stage_s = parts[1];
    lesl::PipelineStage stage = lesl::PipelineStage::Vertex;
    if (stage_s == "vertex" || stage_s == "vert") {
        stage = lesl::PipelineStage::Vertex;
    } else if (stage_s == "fragment" || stage_s == "frag") {
        stage = lesl::PipelineStage::Fragment;
    } else {
        return std::nullopt;
    }

    std::string storage_s = parts[2];
    lesl::StorageClass storage = lesl::StorageClass::Input;
    if (storage_s == "input") {
        storage = lesl::StorageClass::Input;
    } else if (storage_s == "output") {
        storage = lesl::StorageClass::Output;
    } else if (storage_s == "uniform") {
        storage = lesl::StorageClass::Uniform;
    } else if (storage_s == "image" || storage_s == "sampler") {
        storage = lesl::StorageClass::ImageSampler;
    } else if (storage_s == "storage") {
        storage = lesl::StorageClass::StorageBuffer;
    } else {
        return std::nullopt;
    }
    try {
        uint32_t set = std::stoi(parts[3]);
        uint32_t slot = std::stoi(parts[4]);

        return lesl::DictionaryBindingManager::InterfaceBinding{
            name, stage, storage, set, slot,
        };
    } catch (...) {
        return std::nullopt;
    }
}

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
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                args.output = argv[i + 1];
                i++;
            }
        } else if (arg == "--pipeline" || arg == "-p") {
            if (i + 1 < argc) {
                args.pipeline = argv[i + 1];
                i++;
            } else {
                std::cerr << "error: --pipeline requires an argument" << std::endl;
                exit(1);
            }
        } else if (arg == "--bind-style" || arg == "-B") {
            if (i + 1 < argc) {
                std::string type = argv[i + 1];
                if (type == "sdl") {
                    args.binding_manager = BindingManagerType::SDL3;
                } else if (type == "simple") {
                    args.binding_manager = BindingManagerType::Simple;
                } else if (type == "dictionary") {
                    args.binding_manager = BindingManagerType::Dictionary;
                } else {
                    std::cerr << "error: unknown binding type" << std::endl;
                    exit(1);
                }
                i++;
            } else {
                std::cerr << "error: --bind-style requires an argument" << std::endl;
                exit(1);
            }
        } else if (arg == "--bind" || arg == "-b") {
            if (i + 1 < argc) {
                std::string bind = argv[i + 1];
                auto res = parse_bind(bind);
                if (res) {
                    args.dict_binds.push_back(*res);
                } else {
                    std::cerr << "error: bad --bind syntax" << std::endl;
                    exit(1);
                }
                i++;
            } else {
                std::cerr << "error: --bind-style requires an argument" << std::endl;
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

void print_formatted(const spvbc::BinaryContainer& spv) {
    uint32_t i = 5;
    uint32_t opn = 0;
    while (i < spv.words.size()) {
        uint32_t inst = spv.words[i];
        uint32_t word_count = (inst >> 16) & 0xffff;

        std::cout << opn << " " << colorize::cyan("OpID ") << colorize::yellow(inst & 0xffff)
                  << colorize::cyan(" WordCount ") << colorize::yellow(word_count) << ": ";

        for (uint32_t j = 0; j < word_count; j++) {
            printf("%08x ", spv.words[i + j]);
        }

        printf("\n");

        i += word_count;
        opn++;
    }
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

    lesl::CompilationArena arena{};

    lesl::ErrorHandler error_handler;

    lesl::Unit unit(*in);

    lesl::Tokenizer tokenizer(arena, unit, error_handler);

    lesl::Parser parser(arena, tokenizer, error_handler);

    parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    if (args.verbose) {
        std::cout << colorize::green("=== Parsed Declarations ===") << std::endl;
        lesl::ReprPrinter printer{ std::cout };

        for (auto decl : arena.decls) {
            printer.print(*decl);
        }
    }

    uint32_t pipeline_count = 0;
    lesl::Opt<std::string> single_pipeline_name;
    for (auto d : arena.decls) {
        if (d->is<lesl::Decl::Pipeline>()) {
            if (!single_pipeline_name.has_value()) {
                single_pipeline_name = d->get<lesl::Decl::Pipeline>().name.name.to_string();
            }
            pipeline_count += 1;
        }
    }

    if (pipeline_count == 0) {
        std::cout << "File contains no pipelines!" << std::endl;
        return 1;
    }

    if (pipeline_count > 1 && !args.pipeline) {
        std::cout << "File contains multiple pipelines, select one using --pipeline"
                  << std::endl;
        return 1;
    } else if (args.pipeline) {
        single_pipeline_name = args.pipeline;
    }

    lesl::Validator validator(arena, error_handler);

    validator.validate();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    lesl::BindingManagerInterface* binding_manager = nullptr;

    if (!args.binding_manager.has_value() || args.binding_manager == BindingManagerType::SDL3) {
        binding_manager = new lesl::sdl::SDL3BindingManager(
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
        );
    } else if (args.binding_manager == BindingManagerType::Simple) {
        binding_manager = new lesl::SimpleBindingManager(
            lesl::SimpleBindingManager::BindingAllocationMode::SingleInputMultipleUniform
        );
    } else if (args.binding_manager == BindingManagerType::Dictionary) {
        binding_manager = new lesl::DictionaryBindingManager(args.dict_binds);
    }

    lesl::CodeGenerator codegen(arena, *binding_manager, single_pipeline_name);

    codegen.generate();

    if (error_handler.has_errors()) {
        error_handler.dump(std::cerr);
        return 1;
    }

    if (args.verbose) {
        std::cout << colorize::green("=== Generated SPIR-V ===") << std::endl;
        print_formatted(codegen.spv);
    }

#if LESL_ENABLE_OPT
    spvtools::Optimizer optimizer{ spv_target_env::SPV_ENV_VULKAN_1_0 };
    spvtools::OptimizerOptions options;
    options.set_run_validator(true);
    options.set_preserve_bindings(true);

    optimizer.RegisterPerformancePasses();

    std::vector<uint32_t> optimized;
    bool ok =
        optimizer.Run(codegen.spv.words.data(), codegen.spv.words.size(), &optimized, options);

    if (!ok) {
        std::cout << "Optimization failed." << std::endl;
        return 1;
    }

    for (unsigned i = 0; i < optimized.size(); i++) {
        out->write(reinterpret_cast<const char*>(&optimized[i]), sizeof(uint32_t));
    }
#else
    codegen.flush(*out);
#endif

    delete binding_manager;

    arena.clear();

    if (args.input.has_value()) {
        delete in;
    }

    if (args.output.has_value()) {
        delete out;
    }

    return 0;
}
