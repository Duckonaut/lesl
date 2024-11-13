#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include <spirv_binary_container.hpp>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <ostream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

void flush_spv_binary(const spv_binary::BinaryContainer& bc, std::ostream& out) {
    for (uint i = 0; i < bc.words.size(); i++) {
        out.write(reinterpret_cast<const char*>(&bc.words[i]), sizeof(uint32_t));
    }
}

int main(int argc, char* argv[]) {
    spv_binary::BinaryContainer container;

    container.Capability(spv::CapabilityShader);
    container.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);

    uint32_t frag_binds[] = {2, 3};
    uint32_t frag_bind_count = sizeof(frag_binds) / sizeof(frag_binds[0]);

    container.EntryPoint(spv::ExecutionModelFragment, 1, "frag", frag_binds, frag_bind_count);

    uint32_t vert_binds[] = {5, 6, 7, 8};
    uint32_t vert_bind_count = sizeof(vert_binds) / sizeof(vert_binds[0]);

    container.EntryPoint(spv::ExecutionModelVertex, 4, "vert", vert_binds, vert_bind_count);
    container.ExecutionMode(1, spv::ExecutionModeOriginUpperLeft);

    uint32_t locs[] = {0, 1};
    container.Decorate(2, spv::DecorationLocation, &locs[0], 1);
    container.Decorate(3, spv::DecorationLocation, &locs[0], 1);

    uint32_t builtins[] = {spv::BuiltInPosition};
    container.Decorate(5, spv::DecorationBuiltIn, &builtins[0], 1);
    container.Decorate(6, spv::DecorationLocation, &locs[0], 1);
    container.Decorate(7, spv::DecorationLocation, &locs[1], 1);
    container.Decorate(8, spv::DecorationLocation, &locs[0], 1);

    container.TypeVoid(9);
    container.TypeFunction(10, 9, nullptr, 0);
    container.TypeFloat(11, 32);
    container.TypeVector(12, 11, 4);
    container.TypePointer(13, spv::StorageClassOutput, 12);
    container.TypePointer(14, spv::StorageClassInput, 12);

    // fragment shader
    container.Variable(14, 2, spv::StorageClassInput);
    container.Variable(13, 3, spv::StorageClassOutput);

    // vertex shader
    container.Variable(13, 5, spv::StorageClassOutput);
    container.Variable(14, 6, spv::StorageClassInput);
    container.Variable(14, 7, spv::StorageClassInput);
    container.Variable(13, 8, spv::StorageClassOutput);

    float f = 0.5f;
    container.Constant(11, 15, *reinterpret_cast<uint32_t*>(&f));

    uint32_t vec4[] = {15, 15, 15, 15};

    container.ConstantComposite(12, 16, vec4, 4);

    // fragment shader
    container.Function(9, 1, spv::FunctionControlMaskNone, 10);
    container.Label(17);
    container.Load(12, 18, 2, spv::MemoryAccessMaskNone);
    container.FMul(12, 19, 18, 16);
    container.Store(3, 19, spv::MemoryAccessMaskNone);
    container.Return();
    container.FunctionEnd();

    // vertex shader
    container.Function(9, 4, spv::FunctionControlMaskNone, 10);
    container.Label(20);
    container.Load(12, 21, 6, spv::MemoryAccessMaskNone);
    container.Store(5, 21, spv::MemoryAccessMaskNone);
    container.Load(12, 22, 7, spv::MemoryAccessMaskNone);
    container.Store(8, 22, spv::MemoryAccessMaskNone);
    container.Return();
    container.FunctionEnd();

    container.update_bound(23);

#ifdef _WIN32
    // switch to binary mode on Windows
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    flush_spv_binary(container, std::cout);

    return 0;
}
