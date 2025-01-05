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
    }

    void flush(std::ostream& out) {
        for (unsigned i = 0; i < spv.words.size(); i++) {
            out.write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
        }
    }
};
