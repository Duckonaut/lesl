#include "parser.hpp"
#include "repr.hpp"

Parser::Parser(Tokenizer& tokenizer, ErrorHandler& error_handler)
    : tokenizer(tokenizer), error_handler(error_handler) {
    current = tokenizer.next();
}

Module Parser::parse() {
    Module module;


    while (true) {
        if (error_handler.has_errors()) {
            break;
        }

        if (current.type == TokenType::EndOfFile) {
            break;
        }


        if (current.type == TokenType::Function) {
            module.decls.push_back(parse_function());
        } else if (current.type == TokenType::Struct) {
            Decl s = parse_struct();
            module.decls.push_back(std::move(s));
        } else if (current.type == TokenType::Pipeline) {
            module.decls.push_back(parse_pipeline());
        } else {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
        }
    }

    return module;
}

Parser::~Parser() {}

void Parser::expect(TokenType type) {
    if (current.type != type) {
        error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
    }
}

void Parser::consume(TokenType type) {
    expect(type);
    current = tokenizer.next();
}

void Parser::step() {
    current = tokenizer.next();
}

Decl Parser::parse_function() {
    Decl::Function f;

    consume(TokenType::Function);

    expect(TokenType::Identifier);

    f.name.name = current.value.str;

    step();
    consume(TokenType::LeftParen);

    while (current.type != TokenType::RightParen) {
        TypedIdentifier param;

        expect(TokenType::Identifier);
        param.type = current;
        step();
        expect(TokenType::Identifier);
        param.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        }
        else if (current.type != TokenType::RightParen) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        f.params.push_back(param);
    }

    consume(TokenType::RightParen);

    consume(TokenType::MinusArrow);

    consume(TokenType::LeftParen);

    while (current.type != TokenType::RightParen) {
        TypedIdentifier param;

        expect(TokenType::Identifier);
        param.type = current;
        step();
        expect(TokenType::Identifier);
        param.name = current;
        if (current.type == TokenType::Comma) {
            step();
        }
        else if (current.type != TokenType::RightParen) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        f.rets.push_back(param);
    }

    consume(TokenType::RightParen);

    consume(TokenType::LeftBrace);

    while (current.type != TokenType::RightBrace) {
        f.stmts.push_back(parse_stmt());
    }

    consume(TokenType::RightBrace);

    return Decl{std::move(f)};
}

Decl Parser::parse_struct() {
    Decl::Struct s;

    consume(TokenType::Struct);

    expect(TokenType::Identifier);
    s.name = current;
    step();

    consume(TokenType::LeftBrace);

    while (current.type != TokenType::RightBrace) {
        TypedIdentifier param;

        expect(TokenType::Identifier);
        param.type = current;
        step();
        expect(TokenType::Identifier);
        param.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        }
        else if (current.type != TokenType::RightBrace) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        s.members.push_back(param);
    }

    return Decl{std::move(s)};
}

Decl Parser::parse_pipeline() {
    Decl::Pipeline p;

    consume(TokenType::Pipeline);

    expect(TokenType::Identifier);
    p.name = current;
    step();

    consume(TokenType::LeftBrace);

    while (current.type != TokenType::RightBrace) {
        PipelineParameter param;

        expect(TokenType::Identifier);
        param.name = current;
        step();
        consume(TokenType::Equal);
        expect(TokenType::Identifier);
        param.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        }
        else if (current.type != TokenType::RightBrace) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        p.params.push_back(param);
    }

    return Decl{std::move(p)};
}

Stmt Parser::parse_stmt() {
    return Stmt(Stmt::Var());
}
