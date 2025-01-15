#pragma once

#include <ref_container.hpp>
#include "colorize.hpp"
#include "token.hpp"

#include <cassert>
#include <vector>
#include <variant>
#include <optional>

template <typename T>
using Opt = std::optional<T>;

struct Expr;
struct Stmt;
struct Decl;

struct Identifier {
    PoolStr name;
    SourceLocation location;

    Identifier() : name(), location() {}

    Identifier(Token token) {
        assert(token.type == TokenType::Identifier);
        this->name = token.value.str;
        this->location = token.location;
    }
};

struct TypedIdentifier {
    Identifier name;
    Identifier type;
};

struct PipelineParameter {
    Identifier name;
    Identifier value;
};

struct Expr {
    struct Call {
        Ref<Expr> name;
        std::vector<Ref<Expr>> args;
    };

    enum class BinaryOp {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,

        Or,
        And,
        Not,

        Assign,
        AddAssign,
        SubAssign,
        MulAssign,
        DivAssign,
        ModAssign,
    };

    struct Binary {
        BinaryOp op;
        Ref<Expr> lhs;
        Ref<Expr> rhs;
    };

    enum class UnaryOp {
        Neg,
        Not,
    };

    struct Unary {
        UnaryOp op;
        Ref<Expr> expr;
    };

    struct ListAccess {
        Ref<Expr> list;
        Ref<Expr> index;
    };

    struct FieldAccess {
        Ref<Expr> object;
        Identifier field;
    };

    std::variant<Identifier, double, Call, Binary, Unary, ListAccess, FieldAccess> data;

    Expr(Identifier identifier) : data(identifier) {}
    Expr(double number) : data(number) {}
    Expr(Call call) : data(call) {}
    Expr(Binary binary) : data(binary) {}
    Expr(Unary unary) : data(unary) {}
    Expr(ListAccess listAccess) : data(listAccess) {}
    Expr(FieldAccess fieldAccess) : data(fieldAccess) {}

    Expr(const Expr& other) : data(other.data) {}

    template <typename T> bool is() const {
        return std::holds_alternative<T>(data);
    }

    template <typename T> T& get() {
        return std::get<T>(data);
    }
};

struct Stmt {
    struct Return {
        Opt<Ref<Expr>> expr;
    };

    struct Var {
        TypedIdentifier typedIdentifier;
        Opt<Ref<Expr>> expr;
    };

    std::variant<Return, Var, Ref<Expr>> data;

    Stmt(Return return_) : data(return_) {}
    Stmt(Var var) : data(var) {}
    Stmt(Ref<Expr> expr) : data(expr) {}

    template <typename T> bool is() const {
        return std::holds_alternative<T>(data);
    }

    template <typename T> T& get() {
        return std::get<T>(data);
    }
};

struct Decl {
    enum class Kind {
        Import,
        Struct,
        Function,
        Pipeline,
    };

    struct Import {
        PoolStr path;
    };

    struct Struct {
        Identifier name;
        std::vector<TypedIdentifier> members;
    };

    struct Function {
        Identifier name;
        std::vector<TypedIdentifier> params;
        std::vector<TypedIdentifier> rets;
        std::vector<Ref<Stmt>> stmts;
    };

    struct Pipeline {
        Identifier name;
        std::vector<PipelineParameter> params;
    };

    std::variant<Import, Struct, Function, Pipeline> data;

    Decl(Import&& import) : data(import) {}
    Decl(Struct&& struct_) : data(struct_) {}
    Decl(Function&& function) : data(function) {}
    Decl(Pipeline&& pipeline) : data(pipeline) {}

    template <typename T> bool is() const {
        return std::holds_alternative<T>(data);
    }

    template <typename T> T& get() {
        return std::get<T>(data);
    }
};

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

struct ReprPrinter {
    std::ostream& out;
    int indent = 0;

    ReprPrinter(std::ostream& out) : out(out) {}

    void print_indent() {
        for (int i = 0; i < indent; i++) {
            out << "    ";
        }
    }

    void print(const Decl& decl) {
        std::visit(
            [this](const auto& decl) {
                print(decl);
            },
            decl.data
        );
    }

    void print(const Decl::Import& import) {
        out << colorize::magenta("import \"") << import.path.c_str() << "\"";
    }

    void print(const Decl::Struct& struct_) {
        out << colorize::magenta("struct ") << struct_.name.name.c_str() << " {\n";
        indent++;
        for (const TypedIdentifier& member : struct_.members) {
            print_indent();
            out << member.type.name.c_str() << " " << member.name.name.c_str() << ",\n";
        }
        indent--;
        print_indent();
        out << "}\n";
    }

    void print(const Decl::Function& function) {
        out << colorize::magenta("function ") << function.name.name.c_str() << "(";
        for (size_t i = 0; i < function.params.size(); i++) {
            const TypedIdentifier& param = function.params[i];
            out << param.type.name.c_str() << " " << param.name.name.c_str();
            if (i + 1 < function.params.size()) {
                out << ", ";
            }
        }
        out << ") -> (";
        for (size_t i = 0; i < function.rets.size(); i++) {
            const TypedIdentifier& ret = function.rets[i];
            out << ret.type.name.c_str() << " " << ret.name.name.c_str();
            if (i + 1 < function.rets.size()) {
                out << ", ";
            }
        }
        out << ") {\n";
        indent++;
        for (const Ref<Stmt>& stmt : function.stmts) {
            print_indent();
            print(*stmt);
            out << "\n";
        }
        indent--;
        print_indent();
        out << "}\n";
    }

    void print(const Decl::Pipeline& pipeline) {
        out << colorize::magenta("pipeline ") << pipeline.name.name.c_str() << " {\n";
        indent++;
        for (const PipelineParameter& param : pipeline.params) {
            print_indent();
            out << param.name.name.c_str() << " = " << param.value.name.c_str() << ",\n";
        }
        indent--;
        print_indent();
        out << "}\n";
    }

    void print(const Stmt& stmt) {
        std::visit(
            overloaded{
                [this](const Stmt::Return& return_) {
                    print(return_);
                },
                [this](const Stmt::Var& var) {
                    print(var);
                },
                [this](Ref<Expr> expr) {
                    print_expr(*expr);
                }
            },
            stmt.data
        );
    }

    void print(const Stmt::Return& return_) {
        out << colorize::blue("return");
        if (return_.expr.has_value()) {
            out << " ";
            print_expr(*return_.expr.value());
        }
    }

    void print(const Stmt::Var& var) {
        out << colorize::bright_blue(var.typedIdentifier.type.name.c_str()) << " "
            << var.typedIdentifier.name.name.c_str();
        if (var.expr) {
            out << " = ";
            print_expr(**var.expr);
        }
    }

    void print_expr(const Expr& expr) {
        std::visit(
            overloaded{
                [this](const Identifier& identifier) {
                    out << identifier.name.c_str();
                },
                [this](double number) {
                    out << number;
                },
                [this](const Expr::Call& call) {
                    print_call(call);
                },
                [this](const Expr::Binary& binary) {
                    print_binary(binary);
                },
                [this](const Expr::Unary& unary) {
                    print_unary(unary);
                },
                [this](const Expr::ListAccess& listAccess) {
                    print_list_access(listAccess);
                },
                [this](const Expr::FieldAccess& fieldAccess) {
                    print_field_access(fieldAccess);
                },
            },
            expr.data
        );
    }

    void print_call(const Expr::Call& call) {
        print_expr(*call.name);
        out << "(";
        for (size_t i = 0; i < call.args.size(); i++) {
            print_expr(*call.args[i]);
            if (i + 1 < call.args.size()) {
                out << ", ";
            }
        }
        out << ")";
    }

    void print_binary(const Expr::Binary& binary) {
        out << "(";
        print_expr(*binary.lhs);
        switch (binary.op) {
            case Expr::BinaryOp::Add:
                out << " + ";
                break;
            case Expr::BinaryOp::Sub:
                out << " - ";
                break;
            case Expr::BinaryOp::Mul:
                out << " * ";
                break;
            case Expr::BinaryOp::Div:
                out << " / ";
                break;
            case Expr::BinaryOp::Mod:
                out << " % ";
                break;
            case Expr::BinaryOp::Equal:
                out << " == ";
                break;
            case Expr::BinaryOp::NotEqual:
                out << " != ";
                break;
            case Expr::BinaryOp::Less:
                out << " < ";
                break;
            case Expr::BinaryOp::LessEqual:
                out << " <= ";
                break;
            case Expr::BinaryOp::Greater:
                out << " > ";
                break;
            case Expr::BinaryOp::GreaterEqual:
                out << " >= ";
                break;
            case Expr::BinaryOp::Or:
                out << " || ";
                break;
            case Expr::BinaryOp::And:
                out << " && ";
                break;
            case Expr::BinaryOp::Not:
                out << " ! ";
                break;
            case Expr::BinaryOp::Assign:
                out << " = ";
                break;
            case Expr::BinaryOp::AddAssign:
                out << " += ";
                break;
            case Expr::BinaryOp::SubAssign:
                out << " -= ";
                break;
            case Expr::BinaryOp::MulAssign:
                out << " *= ";
                break;
            case Expr::BinaryOp::DivAssign:
                out << " /= ";
                break;
            case Expr::BinaryOp::ModAssign:
                out << " %= ";
                break;
        }
        print_expr(*binary.rhs);
        out << ")";
    }

    void print_unary(const Expr::Unary& unary) {
        switch (unary.op) {
            case Expr::UnaryOp::Neg:
                out << "-";
                break;
            case Expr::UnaryOp::Not:
                out << "!";
                break;
        }
        print_expr(*unary.expr);
    }

    void print_list_access(const Expr::ListAccess& listAccess) {
        print_expr(*listAccess.list);
        out << "[";
        print_expr(*listAccess.index);
        out << "]";
    }

    void print_field_access(const Expr::FieldAccess& fieldAccess) {
        print_expr(*fieldAccess.object);
        out << "." << fieldAccess.field.name.c_str();
    }
};
