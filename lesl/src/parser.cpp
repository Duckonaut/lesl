#include "lesl/parser.hpp"
#include "lesl/repr.hpp"

#include <initializer_list>

namespace lesl {

Parser::Parser(CompilationArena& arena, Tokenizer& tokenizer, ErrorHandler& error_handler)
    : tokenizer(tokenizer), arena(arena), error_handler(error_handler) {
    current = tokenizer.next();
    next = tokenizer.next();
}

void Parser::parse() {
    while (true) {
        if (error_handler.has_errors()) {
            break;
        }

        if (current.type == TokenType::EndOfFile) {
            break;
        }

        if (current.type == TokenType::Function) {
            parse_function();
        } else if (current.type == TokenType::Struct) {
            parse_struct();
        } else if (current.type == TokenType::Pipeline) {
            parse_pipeline();
        } else {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
        }
    }
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

TypeRef Parser::parse_type_ref() {
    TypeRef type;
    expect(TokenType::Identifier);
    type.name = current;
    step();
    while (current.type == TokenType::LeftBracket) {
        step();
        if (current.type == TokenType::RightBracket) {
            type.array_sizes.push_back(-1);
        } else {
            expect(TokenType::Number);
            double num = current.value.num;
            if (num != (uint32_t)num) {
                error_handler.error(ErrorType::InvalidArraySize, current.location);
            }
            type.array_sizes.push_back((uint32_t)current.value.num);
            step();
            expect(TokenType::RightBracket);
        }
        step();
    }
    return type;
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
        param.type = TypeRef{ current };
        step();
        expect(TokenType::Identifier);
        param.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        } else if (current.type != TokenType::RightParen) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        f.params.push_back(param);
    }

    consume(TokenType::RightParen);

    consume(TokenType::MinusArrow);

    consume(TokenType::LeftParen);

    while (current.type != TokenType::RightParen) {
        TypedIdentifier ret;

        ret.type = parse_type_ref();
        expect(TokenType::Identifier);
        ret.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        } else if (current.type != TokenType::RightParen) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        f.rets.push_back(ret);
    }

    consume(TokenType::RightParen);

    f.stmts = parse_stmt_block();

    return arena.alloc(Decl{ std::move(f) });
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

        param.type = parse_type_ref();
        expect(TokenType::Identifier);
        param.name = current;
        step();
        if (current.type == TokenType::Comma) {
            step();
        } else if (current.type != TokenType::RightBrace) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        s.members.push_back(param);
    }

    consume(TokenType::RightBrace);

    return arena.alloc(Decl{ std::move(s) });
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
        if (current.type == TokenType::Number) {
            param.value = current;
        } else {
            expect(TokenType::Identifier);
            param.value = current;
        }
        step();
        if (current.type == TokenType::Comma) {
            step();
        } else if (current.type != TokenType::RightBrace) {
            error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
            break;
        }

        p.params.push_back(param);
    }

    consume(TokenType::RightBrace);

    return arena.alloc(Decl{ std::move(p) });
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
        } else if (
            current.type == TokenType::Identifier && next.type == TokenType::Identifier
        ) {
            stmts.push_back(parse_var());
        } else if (current.type == TokenType::If) {
            stmts.push_back(parse_if_stmt());
        } else if (current.type == TokenType::For) {
            stmts.push_back(parse_for_stmt());
        } else if (current.type == TokenType::Break) {
            stmts.push_back(parse_break());
        } else if (current.type == TokenType::Continue) {
            stmts.push_back(parse_continue());
        } else if (current.type == TokenType::Discard) {
            stmts.push_back(parse_discard());
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
    v.typedIdentifier.type = parse_type_ref();
    expect(TokenType::Identifier);
    v.typedIdentifier.name = current;
    step();
    consume(TokenType::Equal);
    v.expr = parse_expr();

    return arena.alloc(Stmt{ std::move(v) });
}

Ref<Stmt> Parser::parse_return() {
    Stmt::Return r;
    consume(TokenType::Return);
    return arena.alloc(Stmt{ std::move(r) });
}

Ref<Stmt> Parser::parse_if_stmt() {
    consume(TokenType::If);
    Ref<Expr> condition = parse_expr();
    std::vector<Ref<Stmt>> then_branch = parse_stmt_block();
    if (current.type == TokenType::Else) {
        step();
        std::vector<Ref<Stmt>> else_branch = parse_stmt_block();

        return arena.alloc(Stmt{ condition, then_branch, else_branch });
    } else {
        return arena.alloc(Stmt{ condition, then_branch, std::nullopt });
    }
}

Ref<Stmt> Parser::parse_for_stmt() {
    consume(TokenType::For);
    expect(TokenType::Identifier);
    PoolStr iterator_name = current.value.str;
    step();
    consume(TokenType::Equal);

    Ref<Expr> start = parse_expr();

    consume(TokenType::Colon);

    Ref<Expr> end = parse_expr();
    Opt<Ref<Expr>> step_value;
    if (current.type == TokenType::Colon) {
        step();

        step_value = parse_expr();
    }
    std::vector<Ref<Stmt>> body = parse_stmt_block();

    return arena.alloc(Stmt{ iterator_name, start, end, step_value, body });
}

Ref<Stmt> Parser::parse_break() {
    Stmt::Break b;
    consume(TokenType::Break);
    return arena.alloc(Stmt{ std::move(b) });
}

Ref<Stmt> Parser::parse_continue() {
    Stmt::Continue c;
    consume(TokenType::Continue);
    return arena.alloc(Stmt{ std::move(c) });
}

Ref<Stmt> Parser::parse_discard() {
    Stmt::Discard d;
    consume(TokenType::Discard);
    return arena.alloc(Stmt{ std::move(d) });
}

Ref<Stmt> Parser::parse_expr_stmt() {
    return arena.alloc(Stmt{ parse_expr() });
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
                lhs = arena.alloc(Expr{ Expr::Binary{ op, lhs, (this->*parse_next)() } });
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
        return arena.alloc(
            Expr{
                Expr::Unary{
                    Expr::UnaryOp::Neg,
                    parse_unary(),
                },
            }
        );
    } else if (current.type == TokenType::Bang) {
        step();
        return arena.alloc(Expr{ Expr::Unary{ Expr::UnaryOp::Not, parse_unary() } });
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
            e = arena.alloc(Expr{ Expr::FieldAccess{ e, current } });
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
            if (!e->is<Expr::VariableAccess>()) {
                error_handler.error(ErrorType::InvalidFunctionCall, current.location);
            } else {
                const Expr::VariableAccess& var = e->get<Expr::VariableAccess>();
                e = arena.alloc(Expr{ Expr::Call{ var.name, args } });
            }
            step();
        } else if (current.type == TokenType::LeftBracket) {
            step();
            Ref<Expr> index = parse_expr();
            expect(TokenType::RightBracket);
            e = arena.alloc(Expr{ Expr::ListAccess{ e, index } });
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
        return arena.alloc(std::move(e));
    } else if (current.type == TokenType::Number) {
        Expr e{ current.value.num, current.location };
        step();
        return arena.alloc(std::move(e));
    } else if (current.type == TokenType::LeftParen) {
        step();
        Ref<Expr> e = parse_expr();
        consume(TokenType::RightParen);
        return e;
    } else {
        error_handler.error(ErrorType::UnexpectedToken, current.type, current.location);
        return arena.alloc(Expr{ 0, current.location });
    }
}
} // namespace lesl
