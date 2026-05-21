//
// Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
// Copyright (C) 2013-2016 LunarG, Inc.
// Copyright (C) 2016-2020 Google, Inc.
// Modifications Copyright(C) 2021 Advanced Micro Devices, Inc.All rights reserved.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

// this only applies to the standalone wrapper, not the front end in general
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "SPIRV/Logger.h"
#include "lesl/arena.hpp"
#include "lesl/sdl.hpp"
#include <cassert>
#include <functional>
#include <glslang/SPIRV/GlslangToSpv.h>
#endif

#include "glslang/Public/ResourceLimits.h"
#include "./../glslang/Public/ShaderLang.h"
#include "../SPIRV/GLSL.std.450.h"

#include <atomic>
#include <cstdlib>
#include <chrono>

char* ReadFileData(const char* fileName);
void FreeFileData(char* data);
void InfoLogMsg(const char* msg, const char* name, const int num);

// Globally track if any compile or link failure.
std::atomic<int8_t> CompileFailed{ 0 };
std::atomic<int8_t> LinkFailed{ 0 };
std::atomic<int8_t> CompileOrLinkFailed{ 0 };

//
// Give error and exit with failure code.
//
void Error(const char* message, const char* detail = nullptr) {
    fprintf(stderr, "Error: ");
    if (detail != nullptr)
        fprintf(stderr, "%s: ", detail);
    fprintf(stderr, "%s (use -h for usage)\n", message);
    exit(1);
}

// Outputs the given string, but only if it is non-null and non-empty.
// This prevents erroneous newlines from appearing.
void PutsIfNonEmpty(const char* str) {
    if (str && str[0]) {
        puts(str);
    }
}

// Outputs the given string to stderr, but only if it is non-null and non-empty.
// This prevents erroneous newlines from appearing.
void StderrIfNonEmpty(const char* str) {
    if (str && str[0])
        fprintf(stderr, "%s\n", str);
}

void lesl_compilation(const char* data, const char* pipeline, lesl::CompilationArena& arena) {
    auto binding_manager = lesl::sdl::SDL3BindingManager(
        lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
    );

    auto cr = lesl::compile((const char*)data, pipeline, std::move(binding_manager), &arena);
}

void glslang_compilation(const char* data, EShLanguage lang) {
    // keep track of what to free
    std::list<glslang::TShader*> shaders;

    CompileFailed = 0;
    LinkFailed = 0;
    CompileOrLinkFailed = 0;

    glslang::TProgram& program = *new glslang::TProgram;

    glslang::TShader* shader = new glslang::TShader(lang);
    shader->setStrings(&data, 1);

    shader->setEntryPoint("main");

    shader->setEnhancedMsgs();

    shader->setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 450);
    shader->setEnvClient(
        glslang::EShClientVulkan,
        glslang::EShTargetClientVersion::EShTargetVulkan_1_0
    );
    shader->setEnvTarget(
        glslang::EShTargetLanguage::EShTargetSpv,
        glslang::EShTargetLanguageVersion::EShTargetSpv_1_0
    );

    shaders.push_back(shader);

    auto resources = GetDefaultResources();

    if (!shader->parse(resources, 450, false, EShMessages::EShMsgDefault)) {
        printf("Compile failed!\n");

        PutsIfNonEmpty(shader->getInfoLog());
        PutsIfNonEmpty(shader->getInfoDebugLog());

        printf("===============\n");
        CompileFailed = 1;
    }

    program.addShader(shader);

    //
    // Program-level processing...
    //

    // Link
    if (!program.link(EShMessages::EShMsgDefault)) {
        printf("Link failed!\n");
        LinkFailed = true;
    }

    if (!program.mapIO())
        LinkFailed = true;

    // Report
    PutsIfNonEmpty(program.getInfoLog());
    PutsIfNonEmpty(program.getInfoDebugLog());

    std::vector<std::string> outputFiles;

    CompileOrLinkFailed.fetch_or(CompileFailed);
    CompileOrLinkFailed.fetch_or(LinkFailed);
    if (static_cast<bool>(CompileOrLinkFailed.load()))
        printf("SPIR-V is not generated for failed compile or link\n");
    else {
        std::vector<glslang::TIntermediate*> intermediates;
        for (int stage = 0; stage < EShLangCount; ++stage) {
            if (auto* i = program.getIntermediate((EShLanguage)stage)) {
                intermediates.emplace_back(i);
            }
        }
        for (auto& intermediate : intermediates) {
            std::vector<unsigned int> spirv;
            spv::SpvBuildLogger logger;
            glslang::SpvOptions spvOptions;
            spvOptions.generateDebugInfo = true;
            spvOptions.emitNonSemanticShaderDebugInfo = true;
            spvOptions.emitNonSemanticShaderDebugSource = true;

            spvOptions.disableOptimizer = false;
            spvOptions.optimizeSize = false;
            spvOptions.disassemble = false;
            spvOptions.validate = false;
            spvOptions.compileOnly = false;
            glslang::GlslangToSpv(*intermediate, spirv, &logger, &spvOptions);
        }
    }

    CompileOrLinkFailed.fetch_or(CompileFailed);
    CompileOrLinkFailed.fetch_or(LinkFailed);

    // Free everything up, program has to go before the shaders
    // because it might have merged stuff from the shaders, and
    // the stuff from the shaders has to have its destructors called
    // before the pools holding the memory in the shaders is freed.
    delete &program;
    while (shaders.size() > 0) {
        delete shaders.back();
        shaders.pop_back();
    }
}

void measure(std::string pass, int iterations, std::function<void()> f) {
    auto t1 = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        f();
    }

    auto t2 = std::chrono::high_resolution_clock::now();

    std::cout << pass << ": " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1)
              << std::endl;
}

int C_DECL main(int, char*[]) {
    glslang::InitializeProcess();

    char* triangleData =
        ReadFileData("/home/duckonaut/repos/lesl/examples/shaders/lesl/triangle.lesl");
    char* glslTriangleVert =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/triangle.vert");
    char* glslTriangleFrag =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/triangle.frag");

    char* outlineData =
        ReadFileData("/home/duckonaut/repos/lesl/examples/shaders/lesl/outline.lesl");
    char* glslOutlineVert =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/outline.vert");
    char* glslOutlineFrag =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/outline.frag");

    char* ifData = ReadFileData("/home/duckonaut/repos/lesl/examples/shaders/lesl/if.lesl");
    char* glslIfVert =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/if.vert");
    char* glslIfFrag =
        ReadFileData("/home/duckonaut/repos/lesl/tests/glslang-comparison/if.frag");

    lesl::CompilationArena arena;
    const int iterations = 10000;
    measure("lesl triangle", iterations, [&arena, triangleData]() {
        lesl_compilation(triangleData, "Triangle", arena);
    });

    measure("glslang triangle", iterations, [glslTriangleVert, glslTriangleFrag]() {
        glslang_compilation(glslTriangleVert, EShLanguage::EShLangVertex);
        glslang_compilation(glslTriangleFrag, EShLanguage::EShLangFragment);
    });

    measure("lesl outline", iterations, [&arena, outlineData]() {
        lesl_compilation(outlineData, "Basic", arena);
    });

    measure("glslang outline", iterations, [glslOutlineVert, glslOutlineFrag]() {
        glslang_compilation(glslOutlineVert, EShLanguage::EShLangVertex);
        glslang_compilation(glslOutlineFrag, EShLanguage::EShLangFragment);
    });

    measure("lesl if", iterations, [&arena, ifData]() {
        lesl_compilation(ifData, "Basic", arena);
    });

    measure("glslang if", iterations, [glslIfVert, glslIfFrag]() {
        glslang_compilation(glslIfVert, EShLanguage::EShLangVertex);
        glslang_compilation(glslIfFrag, EShLanguage::EShLangFragment);
    });

    FreeFileData(triangleData);
    FreeFileData(glslTriangleVert);
    FreeFileData(glslTriangleFrag);

    FreeFileData(outlineData);
    FreeFileData(glslOutlineVert);
    FreeFileData(glslOutlineFrag);

    FreeFileData(ifData);
    FreeFileData(glslIfVert);
    FreeFileData(glslIfFrag);

    glslang::FinalizeProcess();

    return 0;
}

#if !defined _MSC_VER && !defined MINGW_HAS_SECURE_API

#include <errno.h>

int fopen_s(FILE** pFile, const char* filename, const char* mode) {
    if (!pFile || !filename || !mode) {
        return EINVAL;
    }

    FILE* f = fopen(filename, mode);
    if (!f) {
        if (errno != 0) {
            return errno;
        } else {
            return ENOENT;
        }
    }
    *pFile = f;

    return 0;
}

#endif

//
//   Malloc a string of sufficient size and read a string into it.
//
char* ReadFileData(const char* fileName) {
    FILE* in = nullptr;
    int errorCode = fopen_s(&in, fileName, "r");
    if (errorCode || in == nullptr)
        Error("unable to open input file");

    int count = 0;
    while (fgetc(in) != EOF)
        count++;

    fseek(in, 0, SEEK_SET);

    if (count > 3) {
        unsigned char head[3];
        if (fread(head, 1, 3, in) == 3) {
            if (head[0] == 0xef && head[1] == 0xbb && head[2] == 0xbf) {
                // skip BOM
                count -= 3;
            } else {
                fseek(in, 0, SEEK_SET);
            }
        } else {
            Error("can't read input file");
        }
    }

    char* return_data = (char*)malloc(count + 1); // freed in FreeFileData()
    if ((int)fread(return_data, 1, count, in) != count) {
        free(return_data);
        Error("can't read input file");
    }

    return_data[count] = '\0';
    fclose(in);

    return return_data;
}

void FreeFileData(char* data) {
    free(data);
}

void InfoLogMsg(const char* msg, const char* name, const int num) {
    if (num >= 0)
        printf("#### %s %s %d INFO LOG ####\n", msg, name, num);
    else
        printf("#### %s %s INFO LOG ####\n", msg, name);
}
