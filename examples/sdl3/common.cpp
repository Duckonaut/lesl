#include "common.hpp"

#include "SDL3/SDL_hints.h"
#include "log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstddef>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef RELEASE
#define WINDOW_TITLE PROJECT_NAME
#define EXE_VERSION "v" PROJECT_VERSION
#else
#define WINDOW_TITLE "dev: " PROJECT_NAME
#define EXE_VERSION "v" PROJECT_VERSION " (dev " PROJECT_COMMIT_HASH ")"
#endif

SDL_AppResult SDL_Fail(void) {
    Log::error("SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]);
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event);
SDL_AppResult SDL_AppIterate(void* appstate);
void SDL_AppQuit(void* appstate, SDL_AppResult result);

struct App {
    SDL_Window* window;
    SDL_GPUDevice* device;

    Example* example;

    App(SDL_Window* window,
        SDL_GPUDevice* device,
        Example* example)
        : window(window), device(device), example(example) {}
};

SDL_AppResult SDL_AppInit(void** appstate, int, char* []) {
    Log::init();

    Log::info(PROJECT_NAME " " EXE_VERSION);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return SDL_Fail();
    }

    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    SDL_Window* window = SDL_CreateWindow(WINDOW_TITLE, 512, 512, SDL_WINDOW_RESIZABLE);
    if (!window) {
        return SDL_Fail();
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    if (!device) {
        return SDL_Fail();
    }

    Log::info("GPU device created");

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        Log::error("failed to claim window for GPU device");
        return SDL_Fail();
    }

    Log::info("window claimed for GPU device");

    Example* example = createExample();

    if (!example) {
        Log::error("failed to create example");
        return SDL_APP_FAILURE;
    }

    example->init(window, device);

    App* app = new App(window, device, example);

    *appstate = app;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    App* app = (App*)appstate;
    app->example->event(event);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;

    app->example->update();
    app->example->render(app->window, app->device);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = (App*)appstate;

    app->example->quit(app->device);
    delete app->example;

    SDL_DestroyGPUDevice(app->device);
    SDL_DestroyWindow(app->window);

    delete app;

    Log::info("everything terminated successfully");
    Log::shutdown();
}

FileData readFile(const char *path) {
    const char* base_path = SDL_GetBasePath();

    if (!base_path) {
        Log::error("failed to get base path");
        return { nullptr, 0 };
    }

    std::string full_path = std::string(base_path) + path;

    size_t size;
    void* data = SDL_LoadFile(full_path.c_str(), &size);

    if (!data) {
        Log::error("failed to read file %s", full_path.c_str());
        return { nullptr, 0 };
    }

    return { data, size };
}

void freeFileData(FileData data) {
    SDL_free(data.data);
}

uint8_t* readImageRGBA(const char* path, uint32_t* out_width, uint32_t* out_height) {
    const char* base_path = SDL_GetBasePath();

    if (!base_path) {
        Log::error("failed to get base path");
        return nullptr;
    }

    std::string full_path;
    if (path[0] == '/') {
        full_path = path;
    }
    else {
        full_path = std::string(base_path) + path;
    }

    int width, height, channels;
    uint8_t* data = stbi_load(full_path.c_str(), &width, &height, &channels, 4);

    if (!data) {
        Log::error("failed to read image %s", full_path.c_str());
        return nullptr;
    }

    *out_width = static_cast<uint32_t>(width);
    *out_height = static_cast<uint32_t>(height);

    return data;
}

void freeImageData(uint8_t* data) {
    stbi_image_free(data);
}

bool loadModel(
    const char* path,
    std::vector<ModelVertex>& out_vertices,
    std::vector<uint32_t>& out_indices
) {
    const char* base_path = SDL_GetBasePath();

    if (!base_path) {
        Log::error("failed to get base path");
        return false;
    }

    std::string full_path = std::string(base_path) + path;

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        full_path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs
    );

    if (!scene || !scene->HasMeshes()) {
        Log::error("failed to load model %s", full_path.c_str());
        return false;
    }

    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];

        size_t vertex_offset = vertices.size();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vertex;

            vertex.position = glm::vec3(
                mesh->mVertices[v].x,
                mesh->mVertices[v].y,
                mesh->mVertices[v].z
            );

            if (mesh->HasNormals()) {
                vertex.normal = glm::vec3(
                    mesh->mNormals[v].x,
                    mesh->mNormals[v].y,
                    mesh->mNormals[v].z
                );
            } else {
                vertex.normal = glm::vec3(0.0f, 0.0f, 0.0f);
            }

            if (mesh->HasTextureCoords(0)) {
                vertex.uv = glm::vec2(
                    mesh->mTextureCoords[0][v].x,
                    mesh->mTextureCoords[0][v].y
                );
            } else {
                vertex.uv = glm::vec2(0.0f, 0.0f);
            }

            vertices.push_back(vertex);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            aiFace& face = mesh->mFaces[f];
            for (unsigned int i = 0; i < face.mNumIndices; ++i) {
                indices.push_back(vertex_offset + face.mIndices[i]);
            }
        }
    }

    out_vertices = std::move(vertices);
    out_indices = std::move(indices);

    return true;
}
