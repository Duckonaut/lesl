#pragma once

#include "spirv-tools/libspirv.hpp"
#include <lesl/lesl.hpp>
#include <algorithm>

inline bool spv_validate(const void* code, size_t size) {
    spvtools::SpirvTools tools(spv_target_env::SPV_ENV_VULKAN_1_0);

    return tools.Validate((const uint32_t*)code, size / 4);
}

inline bool has_error(const lesl::CompilationResult& cr, lesl::ErrorType error_type) {
    return !cr.is_ok() &&
           std::any_of(cr.errors.begin(), cr.errors.end(), [error_type](lesl::Error e) {
               return e.type == error_type;
           });
}
