#include "parser.hpp"
#include "repr.hpp"

#include <initializer_list>

Parser::Parser(CompilationArena& arena, Tokenizer& tokenizer, ErrorHandler& error_handler)
    : tokenizer(tokenizer), error_handler(error_handler), arena(arena) {
    current = tokenizer.next();
    next = tokenizer.next();
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
            Ref<Decl> s = parse_struct();
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
    step();
}

void Parser::step() {
    current = next;
    next = tokenizer.next();
}

Ref<Decl> Parser::parse_function() {
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
        step();
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

    f.stmts = std::move(parse_stmt_block());

    return Decl{std::move(f)}.ref(arena);
}

Ref<Decl> Parser::parse_struct() {
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

    consume(TokenType::RightBrace);

    return Decl{std::move(s)}.ref(arena);
}

Ref<Decl> Parser::parse_pipeline() {
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
        param.value = current;
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

    consume(TokenType::RightBrace);

    return Decl{std::move(p)}.ref(arena);
}

std::vector<Ref<Stmt>> Parser::parse_stmt_block() {
    consume(TokenType::LeftBrace);

    std::vector<Ref<Stmt>> stmts;

    while (current.type != TokenType::RightBrace) {
        if (error_handler.has_errors()) {
            break;
        }
        if (current.type == TokenType::Return) {
            stmts.push_back(parse_return());
            // return is the last statement in a block
            break;
        } else if (current.type == TokenType::Identifier &&
                   next.type == TokenType::Identifier) {
            stmts.push_back(parse_var());
        } else {
            stmts.push_back(parse_expr_stmt());
        }
    }

    consume(TokenType::RightBrace);

    return stmts;
}

// Both can start with an identifier
Ref<Stmt> Parser::parse_var() {
    Stmt::Var v;
    expect(TokenType::Identifier);
    v.typedIdentifier.type = current;
    step();
    expect(TokenType::Identifier);
    v.typedIdentifier.name = current;
        step();
        consume(TokenType::Equal);
        v.expr = parse_expr();

    return Stmt{ std::move(v) }.ref(arena);
}

Ref<Stmt> Parser::parse_return() {
    Stmt::Return r;
    consume(TokenType::Return);
    if (current.type != TokenType::RightBrace) {
        r.expr = parse_expr();
    }
    return Stmt{ std::move(r) }.ref(arena);
}

Ref<Stmt> Parser::parse_expr_stmt() {
    return Stmt{ parse_expr() }.ref(arena);
}

Ref<Expr> Parser::parse_expr() {
    return parse_assignment_expr();
}

Ref<Expr> Parser::parse_binary_left_assoc_expr(
    std::initializer_list<std::pair<TokenType, Expr::BinaryOp>> ops,
    Ref<Expr> (Parser::*parse_next)()
) {
    Ref<Expr> lhs = (this->*parse_next)();

    while (true) {
        bool any_match = false;
        for (auto [type, op] : ops) {
            if (current.type == type) {
                any_match = true;
                step();
                lhs = Expr {
                    Expr::Binary {
                        op, lhs, (this->*parse_next)()
                    } }.ref(arena);
                break;
            }
        }

        if (!any_match) {
            break;
        }
    }

    return lhs;
}

Ref<Expr> Parser::parse_assignment_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::Equal, Expr::BinaryOp::Assign },
            { TokenType::PlusEqual, Expr::BinaryOp::AddAssign },
            { TokenType::MinusEqual, Expr::BinaryOp::SubAssign },
            { TokenType::StarEqual, Expr::BinaryOp::MulAssign },
            { TokenType::SlashEqual, Expr::BinaryOp::DivAssign },
            { TokenType::PercentEqual, Expr::BinaryOp::ModAssign },
        },
        &Parser::parse_logical_or_expr
    );
}

Ref<Expr> Parser::parse_logical_or_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::PipePipe, Expr::BinaryOp::Or },
        },
        &Parser::parse_logical_and_expr
    );
}

Ref<Expr> Parser::parse_logical_and_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::AmpAmp, Expr::BinaryOp::And },
        },
        &Parser::parse_equality_expr
    );
}

Ref<Expr> Parser::parse_equality_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::EqualEqual, Expr::BinaryOp::Equal },
            { TokenType::BangEqual, Expr::BinaryOp::NotEqual },
        },
        &Parser::parse_comparison_expr
    );
}

Ref<Expr> Parser::parse_comparison_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::Less, Expr::BinaryOp::Less },
            { TokenType::LessEqual, Expr::BinaryOp::LessEqual },
            { TokenType::Greater, Expr::BinaryOp::Greater },
            { TokenType::GreaterEqual, Expr::BinaryOp::GreaterEqual },
        },
        &Parser::parse_term_expr
    );
}

Ref<Expr> Parser::parse_term_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::Plus, Expr::BinaryOp::Add },
            { TokenType::Minus, Expr::BinaryOp::Sub },
        },
        &Parser::parse_factor_expr
    );
}

Ref<Expr> Parser::parse_factor_expr() {
    return parse_binary_left_assoc_expr(
        {
            { TokenType::Star, Expr::BinaryOp::Mul },
            { TokenType::Slash, Expr::BinaryOp::Div },
            { TokenType::Percent, Expr::BinaryOp::Mod },
        },
        &Parser::parse_unary
    );
}

Ref<Expr> Parser::parse_unary() {
    if (current.type == TokenType::Minus) {
        step();
        return Expr{
            Expr::Unary{ Expr::UnaryOp::Neg, parse_unary(), },
        }
            .ref(arena);
    } else if (current.type == TokenType::Bang) {
        step();
        return Expr{ Expr::Unary{ Expr::UnaryOp::Not, parse_unary() } }.ref(arena);
    } else {
        return parse_access_or_call_or_list_access_or_field_access();
    }
}

Ref<Expr> Parser::parse_access_or_call_or_list_access_or_field_access() {
    Ref<Expr> e = parse_primary();
    while (true) {
        if (current.type == TokenType::Dot) {
            step();
            expect(TokenType::Identifier);
            e = Expr{ Expr::FieldAccess{ e, current } }.ref(arena);
            step();
        } else if (current.type == TokenType::LeftParen) {
            step();
            std::vector<Ref<Expr>> args;
            while (current.type != TokenType::RightParen) {
                args.push_back(parse_expr());
                if (current.type == TokenType::Comma) {
                    step();
                } else if (current.type != TokenType::RightParen) {
                    error_handler
                        .error(ErrorType::UnexpectedToken, current.type, current.location);
                    break;
                }
            }
            e = Expr{ Expr::Call{ e, args } }.ref(arena);
            step();
        } else if (current.type == TokenType::LeftBracket) {
            step();
            Ref<Expr> index = parse_expr();
            expect(TokenType::RightBracket);
            e = Expr {Expr::ListAccess { e, index } }.ref(arena);
            step();
        } else {
            break;
        }
    }
    return e;
}

Ref<Expr> Parser::parse_primary() {
    if (current.type == TokenType::Identifier) {
        Expr e{ current };
        step();
        return e.ref(arena);
    } else if (current.type == TokenType::Number) {
        Expr e{ current.value.num };
        step();
        return e.ref(arena);
    } else if (current.type == TokenType::LeftParen) {
        step();
        Ref<Expr> e = parse_expr();
        consume(TokenType::RightParen);
        return e;
    } else {
        error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
        return Expr{ 0 }.ref(arena);
    }
}
