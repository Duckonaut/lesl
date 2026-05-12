#include "lesl/arena.hpp"
#include "lesl/unit.hpp"
#include "lesl/error_handler.hpp"
#include "lesl/tokenizer.hpp"
#include "lesl/repr.hpp"
#include "lesl/parser.hpp"
#include "lesl/validator.hpp"
#include "lesl/codegen.hpp"
#include <algorithm>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

namespace lesl {

static inline CompilationArena arena;

enum class CompilationResultType {
    Success,
    Failure,
};

struct CompilationResult {
    CompilationResultType type;

    std::vector<char> compiled_program;
    std::map<std::string, std::string> pipeline_parameters;

    static CompilationResult failure() {
        return { CompilationResultType::Failure, {}, {} };
    }
    static CompilationResult
    success(std::vector<char>&& p, std::map<std::string, std::string>&& pp) {
        return { CompilationResultType::Success, std::move(p), std::move(pp) };
    }

    bool is_ok() const {
        return type == CompilationResultType::Success;
    }
};

static inline CompilationResult
compile(const char* program, const char* pipeline, std::ostream* error_output = nullptr) {
    ErrorHandler error_handler;
    std::ostream* error_out = error_output == nullptr ? &std::cerr : error_output;
    error_handler.dump(*error_out);

    std::istringstream in(program);

    Unit unit(in);

    Tokenizer tokenizer(arena, unit, error_handler);

    Parser parser(arena, tokenizer, error_handler);

    parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure();
    }

    Validator validator(arena, error_handler);

    validator.validate();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure();
    }

    SDL3BindingManager binding_manager(
        SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
    );

    CodeGenerator codegen(arena, binding_manager, pipeline);

    codegen.generate();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure();
    }

    std::vector<char> c;

    codegen.flush(c);

    std::map<std::string, std::string> pparams;

    Decl::Pipeline p =
        (*std::find_if(arena.decls.begin(), arena.decls.end(), [&pipeline](Ref<Decl> d) {
            return d->is<Decl::Pipeline>() && d->get<Decl::Pipeline>().name.name == pipeline;
        }))->get<Decl::Pipeline>();

    for (auto& pparam : p.params) {
        pparams[pparam.name.name.to_string()] = pparam.value.name.to_string();
    }

    return CompilationResult::success(std::move(c), std::move(pparams));
}

}; // namespace lesl
