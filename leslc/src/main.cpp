#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include <cstdint>
#include <iostream>
#include <ostream>
#include <vector>

typedef uint32_t SpvWord;

class SpirvBlob {
  public:
    SpirvBlob() = default;
    void push(SpvWord word) {
        words.push_back(word);
    }

    void push_float(float f) {
        push(*reinterpret_cast<SpvWord*>(&f));
    }

    void opcode(spv::Op op, uint32_t wordCount) {
        push(static_cast<SpvWord>(op) | (wordCount << 16));
    }

    void flush(std::ostream& os) {
        for (auto word : words) {
            os.write(reinterpret_cast<const char*>(&word), sizeof(SpvWord));
        }
    }

  private:
    std::vector<SpvWord> words;
};

int main(int argc, char* argv[]) {
    SpirvBlob blob;

    // header
    blob.push(spv::MagicNumber);
    blob.push(spv::Version);

    // generator
    blob.push(0);
    // bound
    blob.push(23);
    // schema
    blob.push(0);

    // instruction stream: each op followed by its operands and then WordCount - 1

    blob.opcode(spv::OpCapability, 2);
    blob.push(spv::CapabilityShader);

    blob.opcode(spv::OpMemoryModel, 3);
    blob.push(spv::AddressingModelLogical);
    blob.push(spv::MemoryModelGLSL450);

    blob.opcode(spv::OpEntryPoint, 7);
    blob.push(spv::ExecutionModelFragment);
    blob.push(1); // entry point id

    const char* fragmentEntryPoint = "frag";
    SpvWord fragmentEntryPointEncoded = *(reinterpret_cast<const SpvWord*>(fragmentEntryPoint));
    blob.push(fragmentEntryPointEncoded);
    blob.push(0); // null terminator

    blob.push(2); // iColor
    blob.push(3); // oColor

    blob.opcode(spv::OpEntryPoint, 9);
    blob.push(spv::ExecutionModelVertex);
    blob.push(4); // entry point id

    const char* vertexEntryPoint = "vert";
    SpvWord vertexEntryPointEncoded = *(reinterpret_cast<const SpvWord*>(vertexEntryPoint));
    blob.push(vertexEntryPointEncoded);
    blob.push(0); // null terminator

    blob.push(5); // gl_Position
    blob.push(6); // iPos
    blob.push(7); // iColor
    blob.push(8); // oColor

    blob.opcode(spv::OpExecutionMode, 3);
    blob.push(1); // entry point id
    blob.push(spv::ExecutionModeOriginUpperLeft);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(2); // iColor
    blob.push(spv::DecorationLocation);
    blob.push(0);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(3); // oColor
    blob.push(spv::DecorationLocation);
    blob.push(0);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(5); // gl_Position
    blob.push(spv::DecorationBuiltIn);
    blob.push(spv::BuiltInPosition);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(6); // iPos
    blob.push(spv::DecorationLocation);
    blob.push(0);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(7); // iColor
    blob.push(spv::DecorationLocation);
    blob.push(1);

    blob.opcode(spv::OpDecorate, 4);
    blob.push(8); // oColor
    blob.push(spv::DecorationLocation);
    blob.push(0);

    blob.opcode(spv::OpTypeVoid, 2);
    blob.push(9); // void type id

    blob.opcode(spv::OpTypeFunction, 3);
    blob.push(10); // function type id
    blob.push(9);  // void type id

    blob.opcode(spv::OpTypeFloat, 3);
    blob.push(11); // float type id
    blob.push(32); // 32-bit float

    blob.opcode(spv::OpTypeVector, 4);
    blob.push(12); // vec4 type id
    blob.push(11); // float type id
    blob.push(4);  // 4 components

    blob.opcode(spv::OpTypePointer, 4);
    blob.push(13); // vec4 pointer type id
    blob.push(spv::StorageClassOutput);
    blob.push(12); // vec4 type id

    blob.opcode(spv::OpTypePointer, 4);
    blob.push(14); // vec4 pointer type id
    blob.push(spv::StorageClassInput);
    blob.push(12); // vec4 type id

    // fragment shader

    blob.opcode(spv::OpVariable, 4);
    blob.push(14); // vec4 input type id
    blob.push(2);  // iColor
    blob.push(spv::StorageClassInput);

    blob.opcode(spv::OpVariable, 4);
    blob.push(13); // vec4 input type id
    blob.push(3);  // oColor
    blob.push(spv::StorageClassOutput);

    // vertex shader

    blob.opcode(spv::OpVariable, 4);
    blob.push(13); // vec4 input type id
    blob.push(5);  // gl_Position
    blob.push(spv::StorageClassOutput);

    blob.opcode(spv::OpVariable, 4);
    blob.push(14); // vec4 input type id
    blob.push(6);  // iPos
    blob.push(spv::StorageClassInput);

    blob.opcode(spv::OpVariable, 4);
    blob.push(14); // vec4 input type id
    blob.push(7);  // iColor
    blob.push(spv::StorageClassInput);

    blob.opcode(spv::OpVariable, 4);
    blob.push(13); // vec4 input type id
    blob.push(8);  // oColor
    blob.push(spv::StorageClassOutput);

    // constants

    blob.opcode(spv::OpConstant, 4);
    blob.push(11); // float type id
    blob.push(15); // float constant id
    blob.push_float(0.5f);

    blob.opcode(spv::OpConstantComposite, 7);
    blob.push(12); // vec4 type id
    blob.push(16); // vec4 constant id
    blob.push(15); // float constant id
    blob.push(15); // float constant id
    blob.push(15); // float constant id
    blob.push(15); // float constant id

    // fragment shader
    blob.opcode(spv::OpFunction, 5);
    blob.push(9); // void type id
    blob.push(1); // fragment function id
    blob.push(spv::FunctionControlMaskNone);
    blob.push(10); // function type id

    blob.opcode(spv::OpLabel, 2);
    blob.push(17); // fragment entry label id

    blob.opcode(spv::OpLoad, 4);
    blob.push(12); // vec4 type id
    blob.push(18); // vec4 variable id
    blob.push(2);  // iColor

    blob.opcode(spv::OpFMul, 5);
    blob.push(12); // vec4 type id
    blob.push(19); // vec4 result id
    blob.push(18); // vec4 variable id
    blob.push(16); // vec4 constant id

    blob.opcode(spv::OpStore, 3);
    blob.push(3);  // oColor
    blob.push(19); // vec4 result id

    blob.opcode(spv::OpReturn, 1);

    blob.opcode(spv::OpFunctionEnd, 1);

    // vertex shader
    blob.opcode(spv::OpFunction, 5);
    blob.push(9); // void type id
    blob.push(4); // vertex function id
    blob.push(spv::FunctionControlMaskNone);
    blob.push(10); // function type id

    blob.opcode(spv::OpLabel, 2);
    blob.push(20); // vertex entry label id

    blob.opcode(spv::OpLoad, 4);
    blob.push(12); // vec4 type id
    blob.push(21); // vec4 variable id
    blob.push(6);  // iPos

    blob.opcode(spv::OpStore, 3);
    blob.push(5);  // oColor
    blob.push(21); // vec4 result id

    blob.opcode(spv::OpLoad, 4);
    blob.push(12); // vec4 type id
    blob.push(22); // vec4 variable id
    blob.push(7);  // iColor

    blob.opcode(spv::OpStore, 3);
    blob.push(8);  // oColor
    blob.push(22); // vec4 result id

    blob.opcode(spv::OpReturn, 1);

    blob.opcode(spv::OpFunctionEnd, 1);

    blob.flush(std::cout);

    return 0;
}
