#pragma once

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
#include <ostream>
#include <sstream>
#include <string>

namespace lesl {

static inline CompilationArena arena;

enum class CompilationResultType {
    Success,
    Failure,
};

struct StageBinds {
    std::vector<Binding> binds;
    uint32_t num_samplers = 0;
    uint32_t num_uniform_buffers = 0;
};

struct CompilationResult {
    CompilationResultType type;

    std::vector<char> compiled_program;
    std::unordered_map<std::string, std::string> pipeline_parameters;

    StageBinds vertex_binds;
    StageBinds fragment_binds;

    static CompilationResult failure() {
        return { CompilationResultType::Failure, {}, {}, {}, {} };
    }
    static CompilationResult success(
        std::vector<char>&& p,
        std::unordered_map<std::string, std::string>&& pp,
        BindingManagerInterface& binding_manager
    ) {
        StageBinds vertex_binds{};
        StageBinds fragment_binds{};

        std::vector<Binding> bindings = binding_manager.get_bindings();

        for (auto& b : bindings) {
            if (b.stage == PipelineStage::Vertex) {
                vertex_binds.binds.push_back(b);
                if (b.type == BindType::Sampler) {
                    vertex_binds.num_samplers++;
                } else if (b.type == BindType::Uniform) {
                    vertex_binds.num_uniform_buffers++;
                }
            } else if (b.stage == PipelineStage::Fragment) {
                fragment_binds.binds.push_back(b);
                if (b.type == BindType::Sampler) {
                    fragment_binds.num_samplers++;
                } else if (b.type == BindType::Uniform) {
                    fragment_binds.num_uniform_buffers++;
                }
            }
        }

        return { CompilationResultType::Success,
                 std::move(p),
                 std::move(pp),
                 std::move(vertex_binds),
                 std::move(fragment_binds) };
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

    std::unordered_map<std::string, std::string> pparams;

    Decl::Pipeline p =
        (*std::find_if(arena.decls.begin(), arena.decls.end(), [&pipeline](Ref<Decl> d) {
            return d->is<Decl::Pipeline>() && d->get<Decl::Pipeline>().name.name == pipeline;
        }))->get<Decl::Pipeline>();

    for (auto& pparam : p.params) {
        pparams[pparam.name.name.to_string()] = pparam.value.name.to_string();
    }

    return CompilationResult::success(std::move(c), std::move(pparams), binding_manager);
}

}; // namespace lesl
