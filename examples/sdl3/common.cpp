#include "common.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstddef>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef RELEASE
#define WINDOW_TITLE PROJECT_NAME
#define GAME_VERSION "v" PROJECT_VERSION
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

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    Log::init();

    Log::info(PROJECT_NAME " " EXE_VERSION);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return SDL_Fail();
    }

    SDL_Window* window = SDL_CreateWindow(WINDOW_TITLE, 720, 540, SDL_WINDOW_RESIZABLE);
    if (!window) {
        return SDL_Fail();
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
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

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
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
