#pragma once

#include <SDL3/SDL.h>
#include <lesl/lesl.hpp>

namespace lesl::sdl {
SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    CompilationResult cr,
    std::vector<SDL_GPUTextureFormat> color_target_formats,
    std::vector<SDL_GPUVertexAttribute>* vertex_attributes = nullptr,
    std::vector<SDL_GPUVertexBufferDescription>* vertex_buffer_descriptions = nullptr
);
}
