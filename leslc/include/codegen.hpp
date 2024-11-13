#pragma once

#include "parser.hpp"
#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include "spirv_binary_container.hpp"
#include <ostream>

class CodeGenerator final {
  public:
    spv_binary::BinaryContainer spv;
    Parser& parser;

    CodeGenerator(Parser& parser) : parser(parser) {}

    void generate() {
        spv.Capability(spv::CapabilityShader);
        spv.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);

        uint32_t frag_binds[] = { 2, 3 };
        uint32_t frag_bind_count = sizeof(frag_binds) / sizeof(frag_binds[0]);

        spv.EntryPoint(spv::ExecutionModelFragment, 1, "frag", frag_binds, frag_bind_count);

        uint32_t vert_binds[] = { 5, 6, 7, 8 };
        uint32_t vert_bind_count = sizeof(vert_binds) / sizeof(vert_binds[0]);

        spv.EntryPoint(spv::ExecutionModelVertex, 4, "vert", vert_binds, vert_bind_count);
        spv.ExecutionMode(1, spv::ExecutionModeOriginUpperLeft);

        uint32_t locs[] = { 0, 1 };
        spv.Decorate(2, spv::DecorationLocation, &locs[0], 1);
        spv.Decorate(3, spv::DecorationLocation, &locs[0], 1);

        uint32_t builtins[] = { spv::BuiltInPosition };
        spv.Decorate(5, spv::DecorationBuiltIn, &builtins[0], 1);
        spv.Decorate(6, spv::DecorationLocation, &locs[0], 1);
        spv.Decorate(7, spv::DecorationLocation, &locs[1], 1);
        spv.Decorate(8, spv::DecorationLocation, &locs[0], 1);

        spv.TypeVoid(9);
        spv.TypeFunction(10, 9, nullptr, 0);
        spv.TypeFloat(11, 32);
        spv.TypeVector(12, 11, 4);
        spv.TypePointer(13, spv::StorageClassOutput, 12);
        spv.TypePointer(14, spv::StorageClassInput, 12);

        // fragment shader
        spv.Variable(14, 2, spv::StorageClassInput);
        spv.Variable(13, 3, spv::StorageClassOutput);

        // vertex shader
        spv.Variable(13, 5, spv::StorageClassOutput);
        spv.Variable(14, 6, spv::StorageClassInput);
        spv.Variable(14, 7, spv::StorageClassInput);
        spv.Variable(13, 8, spv::StorageClassOutput);

        float f = 0.5f;
        spv.Constant(11, 15, *reinterpret_cast<uint32_t*>(&f));

        uint32_t vec4[] = { 15, 15, 15, 15 };

        spv.ConstantComposite(12, 16, vec4, 4);

        // fragment shader
        spv.Function(9, 1, spv::FunctionControlMaskNone, 10);
        spv.Label(17);
        spv.Load(12, 18, 2, spv::MemoryAccessMaskNone);
        spv.FMul(12, 19, 18, 16);
        spv.Store(3, 19, spv::MemoryAccessMaskNone);
        spv.Return();
        spv.FunctionEnd();

        // vertex shader
        spv.Function(9, 4, spv::FunctionControlMaskNone, 10);
        spv.Label(20);
        spv.Load(12, 21, 6, spv::MemoryAccessMaskNone);
        spv.Store(5, 21, spv::MemoryAccessMaskNone);
        spv.Load(12, 22, 7, spv::MemoryAccessMaskNone);
        spv.Store(8, 22, spv::MemoryAccessMaskNone);
        spv.Return();
        spv.FunctionEnd();

        spv.update_bound(23);
    }

    void flush(std::ostream& out) {
        for (uint i = 0; i < spv.words.size(); i++) {
            out.write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
        }
    }
};
