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
#include <fstream>
#include <iostream>
#include <json/writer.h>
#include <ostream>
#include <sstream>
#include <string>

#if LESL_ENABLE_OPT
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>
#endif

#include <json/json.h>

namespace lesl {

static inline CompilationArena g_arena;

enum class CompilationResultType {
    Success,
    Failure,
};

struct StageBinds {
    std::vector<Binding> binds;
    uint32_t num_samplers = 0;
    uint32_t num_uniform_buffers = 0;
    uint32_t num_storage_buffers = 0;
};

struct CompilationResult {
    CompilationResultType type;

    std::vector<lesl::Error> errors;

    std::vector<char> compiled_program;
    std::unordered_map<std::string, std::string> pipeline_parameters;

    StageBinds vertex;
    StageBinds fragment;

    static CompilationResult failure(const ErrorHandler& error_handler) {
        return { CompilationResultType::Failure, error_handler.errors, {}, {}, {}, {} };
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
                else if (b.type == BindType::Storage) {
                    vertex_binds.num_storage_buffers++;
                }
            } else if (b.stage == PipelineStage::Fragment) {
                fragment_binds.binds.push_back(b);
                if (b.type == BindType::Sampler) {
                    fragment_binds.num_samplers++;
                } else if (b.type == BindType::Uniform) {
                    fragment_binds.num_uniform_buffers++;
                }
                else if (b.type == BindType::Storage) {
                    fragment_binds.num_storage_buffers++;
                }
            }
        }

        return {
            CompilationResultType::Success,
            {},
            std::move(p),
            std::move(pp),
            std::move(vertex_binds),
            std::move(fragment_binds),
        };
    }

    bool is_ok() const {
        return type == CompilationResultType::Success;
    }

    bool write_metadata(const char* path) {
        if (!is_ok()) {
            return false;
        }

        Json::Value root;
        root["pipeline_parameters"] = Json::Value(Json::objectValue);

        for (const auto& pp : pipeline_parameters) {
            root["pipeline_parameters"][pp.first] = pp.second;
        }

        root["vertex"] = Json::Value(Json::objectValue);

        root["vertex"]["num_samplers"] = vertex.num_samplers;
        root["vertex"]["num_uniform_buffers"] = vertex.num_uniform_buffers;

        root["vertex"]["bindings"] = Json::Value(Json::arrayValue);
        for (const auto& b : vertex.binds) {
            Json::Value jb{Json::objectValue};

            jb["binding_type"] = b.binding_type;
            jb["name"] = b.name;
            jb["type"] = bind_type_to_str(b.type);
            jb["set"] = b.set;
            jb["slot"] = b.slot;
            jb["size"] = b.size;
            jb["alignment"] = b.alignment;
            jb["stage"] = "vertex";

            root["vertex"]["bindings"].append(jb);
        }


        root["fragment"] = Json::Value(Json::objectValue);

        root["fragment"]["num_samplers"] = fragment.num_samplers;
        root["fragment"]["num_uniform_buffers"] = fragment.num_uniform_buffers;

        root["fragment"]["bindings"] = Json::Value(Json::arrayValue);
        for (const auto& b : fragment.binds) {
            Json::Value jb{Json::objectValue};

            jb["binding_type"] = b.binding_type;
            jb["name"] = b.name;
            jb["type"] = bind_type_to_str(b.type);
            jb["set"] = b.set;
            jb["slot"] = b.slot;
            jb["size"] = b.size;
            jb["alignment"] = b.alignment;
            jb["stage"] = "fragment";

            root["fragment"]["bindings"].append(jb);
        }
        Json::FastWriter writer;
        std::string s = writer.write(root);
        
        std::ofstream of{path};
        if (!of.good()) {
            return false;
        }

        of << s;

        return true;
    }
};

static inline CompilationResult compile(
    const char* program,
    const char* pipeline,
    BindingManagerInterface&& binding_manager,
    CompilationArena* arena = nullptr,
    std::ostream* error_output = nullptr
) {
    if (arena == nullptr) {
        arena = &g_arena;
    }
    arena->clear();

    ErrorHandler error_handler;
    std::ostream* error_out = error_output == nullptr ? &std::cerr : error_output;
    error_handler.dump(*error_out);

    std::istringstream in(program);

    Unit unit(in);

    Tokenizer tokenizer(*arena, unit, error_handler);

    Parser parser(*arena, tokenizer, error_handler);

    parser.parse();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure(error_handler);
    }

    uint32_t pipeline_count = 0;
    lesl::Opt<std::string> single_pipeline_name;
    for (auto d : arena->decls) {
        if (d->is<lesl::Decl::Pipeline>()) {
            if (!single_pipeline_name.has_value()) {
                single_pipeline_name = d->get<lesl::Decl::Pipeline>().name.name.to_string();
            }
            pipeline_count += 1;
        }
    }

    if (pipeline == nullptr) {
        if (pipeline_count == 1) {
            pipeline = single_pipeline_name->c_str();
        } else {
            return CompilationResult::failure(error_handler);
        }
    }

    Validator validator(*arena, pipeline, error_handler);

    validator.validate();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure(error_handler);
    }

    CodeGenerator codegen(*arena, binding_manager, pipeline);

    codegen.generate();

    if (error_handler.has_errors()) {
        error_handler.dump(*error_out);
        return CompilationResult::failure(error_handler);
    }

    std::vector<char> c;
#if LESL_ENABLE_OPT
    spvtools::Optimizer optimizer{ spv_target_env::SPV_ENV_VULKAN_1_0 };
    spvtools::OptimizerOptions options;
    options.set_run_validator(false);
    options.set_preserve_bindings(true);

    optimizer.RegisterPerformancePasses();

    std::vector<uint32_t> optimized;
    bool ok =
        optimizer.Run(codegen.spv.words.data(), codegen.spv.words.size(), &optimized, options);

    if (!ok) {
        return CompilationResult::failure(error_handler);
    }

    for (unsigned i = 0; i < optimized.size(); i++) {
        for (unsigned j = 0; j < sizeof(uint32_t); j++) {
            c.push_back(reinterpret_cast<const char*>(&optimized[i])[j]);
        }
    }
#else
    codegen.flush(c);
#endif

    std::unordered_map<std::string, std::string> pparams;

    Decl::Pipeline p =
        (*std::find_if(arena->decls.begin(), arena->decls.end(), [&pipeline](Ref<Decl> d) {
            return d->is<Decl::Pipeline>() && d->get<Decl::Pipeline>().name.name == pipeline;
        }))->get<Decl::Pipeline>();

    for (auto& pparam : p.params) {
        pparams[pparam.name.name.to_string()] = pparam.value.name.to_string();
    }

    return CompilationResult::success(std::move(c), std::move(pparams), binding_manager);
}

}; // namespace lesl
