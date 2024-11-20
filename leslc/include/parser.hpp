#pragma once

#include "tokenizer.hpp"
#include "repr.hpp"

struct Parser final {
    Tokenizer& tokenizer;

    Parser(Tokenizer& tokenizer);
    ~Parser();

    Module parse();

    Decl parse_function();
    Decl parse_struct();
    Decl parse_pipeline();
};
