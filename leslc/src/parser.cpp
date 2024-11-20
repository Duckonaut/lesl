#include "parser.hpp"
#include "repr.hpp"

Parser::Parser(Tokenizer& tokenizer) : tokenizer(tokenizer) {}

Module Parser::parse() {
    Module module;

    while (true) {
        Token token = tokenizer.next();

        if (token.type == TokenType::EndOfFile) {
            break;
        }

        if (token.type == TokenType::Function) {
            module.decls.push_back(parse_function());
        } else if (token.type == TokenType::Struct) {
            module.decls.push_back(parse_struct());
        } else if (token.type == TokenType::Pipeline) {
            module.decls.push_back(parse_pipeline());
        } else {
            // error
        }
    }

    return module;
}

Parser::~Parser() {}

Decl Parser::parse_function() {
    Decl::Function f;

    Token token = tokenizer.next();

    if (token.type != TokenType::Identifier) {
        // error
    }

    f.name.name = token.value.str;

    return Decl(f);
}

Decl Parser::parse_struct() {
    Decl::Struct s;

    return Decl(s);
}

Decl Parser::parse_pipeline() {
    Decl::Pipeline p;

    return Decl(p);
}
