#include <fstream>
#include <istream>
#include <optional>

#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"
#include <spirv_binary_container.hpp>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <ostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

struct Args {
    std::optional<std::string> output;
};

Args parse_args(int argc, char* argv[]) {
    Args args;

    if (argc < 2) {
        return args; // read from stdin, write to stdout
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [-o output]" << std::endl;
            exit(0);
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                args.output = argv[i + 1];
                i++;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    return args;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    std::ostream* out = args.output.has_value()
                            ? new std::ofstream(args.output.value(), std::ios::binary)
                            : &std::cout;

    if (!out->good()) {
        std::cerr << "error: failed to open output file" << std::endl;
        return 1;
    }

#ifdef _WIN32
    // switch to binary mode on Windows
    if (!args.output.has_value()) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    spv_binary::BinaryContainer spv;

    spv.Capability(spv::CapabilityShader);
    spv.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);

    spv::Id fiColor = spv.get_id();
    spv::Id foColor = spv.get_id();

    spv::Id viPos = spv.get_id();
    spv::Id voPos = spv.get_id();
    spv::Id viColor = spv.get_id();
    spv::Id voColor = spv.get_id();

    spv::Id frag_fn = spv.get_id();
    spv::Id vert_fn = spv.get_id();

    uint32_t frag_binds[] = { fiColor, foColor };
    uint32_t frag_bind_count = sizeof(frag_binds) / sizeof(frag_binds[0]);

    spv.EntryPoint(spv::ExecutionModelFragment, frag_fn, "frag", frag_binds, frag_bind_count);

    uint32_t vert_binds[] = { viPos, voPos, viColor, voColor };
    uint32_t vert_bind_count = sizeof(vert_binds) / sizeof(vert_binds[0]);

    spv.EntryPoint(spv::ExecutionModelVertex, vert_fn, "vert", vert_binds, vert_bind_count);
    spv.ExecutionMode(frag_fn, spv::ExecutionModeOriginUpperLeft);

    uint32_t locs[] = { 0, 1 };
    spv.Decorate(fiColor, spv::DecorationLocation, &locs[0], 1);
    spv.Decorate(foColor, spv::DecorationLocation, &locs[0], 1);

    uint32_t builtins[] = { spv::BuiltInPosition };
    spv.Decorate(voPos, spv::DecorationBuiltIn, builtins, 1);
    spv.Decorate(viPos, spv::DecorationLocation, &locs[0], 1);
    spv.Decorate(viColor, spv::DecorationLocation, &locs[1], 1);
    spv.Decorate(voColor, spv::DecorationLocation, &locs[0], 1);

    spv::Id void_ = spv.TypeVoidNew();
    spv::Id fn = spv.TypeFunctionNew(void_, nullptr, 0);
    spv::Id fl32 = spv.TypeFloatNew(32);
    spv::Id vec4f32 = spv.TypeVectorNew(fl32, 4);
    spv::Id outVec4 = spv.TypePointerNew(spv::StorageClassOutput, vec4f32);
    spv::Id inVec4 = spv.TypePointerNew(spv::StorageClassInput, vec4f32);

    // fragment shader
    spv.Variable(inVec4, fiColor, spv::StorageClassInput);
    spv.Variable(outVec4, foColor, spv::StorageClassOutput);

    // vertex shader
    spv.Variable(inVec4, viPos, spv::StorageClassInput);
    spv.Variable(inVec4, viColor, spv::StorageClassInput);
    spv.Variable(outVec4, voPos, spv::StorageClassOutput);
    spv.Variable(outVec4, voColor, spv::StorageClassOutput);

    float f = 1.0f;
    spv::Id f0_5 = spv.ConstantNew(fl32, *reinterpret_cast<uint32_t*>(&f));

    uint32_t vec4_0_5_binds[] = { f0_5, f0_5, f0_5, f0_5 };
    spv::Id vec4_0_5 = spv.ConstantCompositeNew(vec4f32, vec4_0_5_binds, 4);

    f = 0.0f;
    spv::Id f0 = spv.ConstantNew(fl32, *reinterpret_cast<uint32_t*>(&f));

    uint32_t vec4_0_binds[] = { f0, f0, f0, f0 };
    spv::Id vec4_0 = spv.ConstantCompositeNew(vec4f32, vec4_0_binds, 4);

    // fragment shader
    spv.Function(void_, frag_fn, spv::FunctionControlMaskNone, fn);
    spv::Id frag_begin_label = spv.LabelNew();
    spv::Id fCol = spv.LoadNew(vec4f32, fiColor, spv::MemoryAccessMaskNone);
    spv::Id fTemp = spv.FMulNew(vec4f32, fCol, vec4_0_5);
    spv.Store(foColor, fTemp, spv::MemoryAccessMaskNone);
    spv.Return();
    spv.FunctionEnd();

    // vertex shader
    spv.Function(void_, vert_fn, spv::FunctionControlMaskNone, fn);
    spv::Id vert_begin_label = spv.LabelNew();
    spv::Id vCol = spv.LoadNew(vec4f32, viColor, spv::MemoryAccessMaskNone);
    spv.Store(voColor, vCol, spv::MemoryAccessMaskNone);
    spv::Id vPos = spv.LoadNew(vec4f32, viPos, spv::MemoryAccessMaskNone);
    spv::Id neg = spv.FSubNew(vec4f32, vec4_0, vPos);
    spv.Store(voPos, neg, spv::MemoryAccessMaskNone);
    spv.Return();
    spv.FunctionEnd();

    spv.update_bound();

    for (unsigned i = 0; i < spv.words.size(); i++) {
        out->write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
    }

    if (args.output.has_value()) {
        delete out;
    }

    return 0;
}
