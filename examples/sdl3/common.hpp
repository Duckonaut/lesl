#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

class Example {
    public:
        virtual ~Example() = default;
        virtual void init(SDL_Window* window, SDL_GPUDevice* device) = 0;
        virtual void event(SDL_Event* event) = 0;
        virtual void update() = 0;
        virtual void render(SDL_Window* window, SDL_GPUDevice* device) = 0;
        virtual void quit(SDL_GPUDevice* device) = 0;
};

Example* createExample();

struct FileData {
    void* data;
    size_t size;
};

FileData readFile(const char* path);
void freeFileData(FileData data);

uint8_t* readImageRGBA(const char* path, uint32_t* width, uint32_t* height);
void freeImageData(uint8_t* data);
