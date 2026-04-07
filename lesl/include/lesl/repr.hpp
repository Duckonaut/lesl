#pragma once

#include "spirv/unified1/spirv.hpp"
#include "lesl/ref_container.hpp"
#include "lesl/colorize.hpp"
#include "lesl/stringpool.hpp"
#include "lesl/token.hpp"

#include <cassert>
#include <vector>
#include <map>
#include <variant>
#include <optional>

template <typename T> using Opt = std::optional<T>;

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

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

    Identifier(PoolStr name, SourceLocation location) : name(name), location(location) {}
};

struct TypeInfo {
    PoolStr name;
    uint32_t size;
    uint32_t alignment;
    uint32_t id;

    std::map<uint32_t, uint32_t> pointer_type_ids;

    enum class BuiltinPrimitive {
        Void,
        Bool,
        Int,
        Uint,
        Float,
    };

    struct Primitive {
        BuiltinPrimitive primitive;
    };

    struct Vector {
        Ref<TypeInfo> element;
        uint32_t size;
    };

    struct Matrix {
        Ref<TypeInfo> vector_element;
        uint32_t columns;
    };

    struct Struct {
        struct Member {
            PoolStr name;
            Ref<TypeInfo> type;
        };

        std::vector<Member> members;
    };

    struct Array {
        Ref<TypeInfo> element;
        bool is_sized;
        uint32_t size;
    };

    struct ImageSampler {
        // Placeholder for future image type representation.
        // TODO: dimension, multisampled
        uint8_t _unused;
    };

    std::variant<Primitive, Struct, Array, Vector, Matrix, ImageSampler> data;

    TypeInfo() : name(), size(0), alignment(0), id(0) {}
    TypeInfo(Primitive&& primitive) : data(primitive) {}
    TypeInfo(Struct&& struct_) : data(struct_) {}
    TypeInfo(Array&& array) : data(array) {}
    TypeInfo(Vector&& vector) : data(vector) {}
    TypeInfo(Matrix&& matrix) : data(matrix) {}
    TypeInfo(ImageSampler&& image_sampler) : data(image_sampler) {}

    static TypeInfo create_primitive(StringPool& pool, BuiltinPrimitive primitive) {
        TypeInfo type{ Primitive{ primitive } };
        switch (primitive) {
            case BuiltinPrimitive::Void:
                type.name = pool.add("void");
                type.size = 0;
                type.alignment = 0;
                break;
            case BuiltinPrimitive::Bool:
                type.name = pool.add("bool");
                type.size = 1;
                type.alignment = 1;
                break;
            case BuiltinPrimitive::Int:
                type.name = pool.add("int");
                type.size = 4;
                type.alignment = 4;
                break;
            case BuiltinPrimitive::Uint:
                type.name = pool.add("uint");
                type.size = 4;
                type.alignment = 4;
                break;
            case BuiltinPrimitive::Float:
                type.name = pool.add("float");
                type.size = 4;
                type.alignment = 4;
                break;
        }

        return type;
    }

    static TypeInfo create_vector(StringPool& pool, Ref<TypeInfo> element, uint32_t size) {
        TypeInfo type{ Vector{ element, size } };
        type.name = pool.add(std::string(element->name.c_str()) + std::to_string(size));
        type.size = element->size * size;
        type.alignment = element->alignment;
        return type;
    }

    static TypeInfo
    create_matrix(StringPool& pool, Ref<TypeInfo> vector_element, uint32_t columns) {
        TypeInfo type{ Matrix{ vector_element, columns } };
        type.name =
            pool.add(std::string(vector_element->name.c_str()) + "x" + std::to_string(columns));
        type.size = vector_element->size * columns;
        type.alignment = vector_element->alignment;
        return type;
    }

    static TypeInfo create_struct(PoolStr name, std::vector<Struct::Member> members) {
        TypeInfo type{ Struct{ members } };
        type.name = name;
        type.size = 0;
        type.alignment = 0;
        for (const auto& member : members) {
            type.size += member.type->size;
            type.alignment = std::max(type.alignment, member.type->alignment);
        }
        return type;
    }

    static TypeInfo
    create_array(StringPool& pool, Ref<TypeInfo> element, bool is_sized, uint32_t size) {
        TypeInfo type{ Array{ element, is_sized, size } };
        type.name = pool.add(
            std::string(element->name.c_str()) +
            (is_sized ? "[" + std::to_string(size) + "]" : "[]")
        );
        if (is_sized) {
            type.size = element->size * size;
        } else {
            type.size = 0;
        }
        type.alignment = element->alignment;
        return type;
    }

    static TypeInfo create_image_sampler(StringPool& pool) {
        TypeInfo type{ ImageSampler{} };
        type.name = pool.add("sampler2D");
        type.size = 0;
        type.alignment = 0;
        return type;
    }

    static const char* builtin_primitive_str(BuiltinPrimitive p) {
        switch (p) {
            case BuiltinPrimitive::Void:
                return "void";
            case BuiltinPrimitive::Bool:
                return "bool";
            case BuiltinPrimitive::Int:
                return "int";
            case BuiltinPrimitive::Uint:
                return "uint";
            case BuiltinPrimitive::Float:
                return "float";
        }
    }

    template <typename T> bool is() const {
        return std::holds_alternative<T>(data);
    }

    template <typename T> T& get() {
        return std::get<T>(data);
    }

    template <typename T> const T& get() const {
        return std::get<T>(data);
    }

    Primitive get_underlying_primitive() const {
        if (is<Primitive>()) {
            return get<Primitive>();
        } else if (is<Vector>()) {
            return get<Vector>().element->get_underlying_primitive();
        } else if (is<Matrix>()) {
            return get<Matrix>().vector_element->get_underlying_primitive();
        } else if (is<Array>()) {
            return get<Array>().element->get_underlying_primitive();
        } else if (is<Struct>() || is<ImageSampler>()) {
            return Primitive{ BuiltinPrimitive::Void };
        } else {
            assert(false);
        }
    }

    uint32_t get_pointer_type(spv::StorageClass storage_class) const {
        if (this->pointer_type_ids.contains(storage_class)) {
            return this->pointer_type_ids.find(static_cast<uint32_t>(storage_class))->second;
        } else {
            assert(false);
        }
    }

    void add_pointer_type(spv::StorageClass storage_class, uint32_t id) {
        this->pointer_type_ids.insert({ static_cast<uint32_t>(storage_class), id });
    }
};

inline bool operator==(const TypeInfo& lhs, const TypeInfo& rhs) {
    return lhs.name == rhs.name && lhs.size == rhs.size && lhs.alignment == rhs.alignment;
}

inline std::ostream& operator<<(std::ostream& out, const TypeInfo& type) {
    std::visit(
        overloaded{
            [&out](const TypeInfo::Primitive& builtin) {
                switch (builtin.primitive) {
                    case TypeInfo::BuiltinPrimitive::Void:
                        out << "void";
                        break;
                    case TypeInfo::BuiltinPrimitive::Bool:
                        out << "bool";
                        break;
                    case TypeInfo::BuiltinPrimitive::Int:
                        out << "int";
                        break;
                    case TypeInfo::BuiltinPrimitive::Uint:
                        out << "uint";
                        break;
                    case TypeInfo::BuiltinPrimitive::Float:
                        out << "float";
                        break;
                }
            },
            [&out](const TypeInfo::Vector& vector) {
                out << *vector.element << vector.size;
            },
            [&out](const TypeInfo::Matrix& matrix) {
                out << *matrix.vector_element << "x" << matrix.columns;
            },
            [&out](const TypeInfo::Struct& struct_) {
                out << "struct { ";
                for (size_t i = 0; i < struct_.members.size(); i++) {
                    out << *struct_.members[i].type << " " << struct_.members[i].name.c_str();
                    if (i + 1 < struct_.members.size()) {
                        out << ", ";
                    }
                }
                out << " }";
            },
            [&out](const TypeInfo::Array& array) {
                out << *array.element;
                if (array.is_sized) {
                    out << "[" << array.size << "]";
                } else {
                    out << "[]";
                }
            },
            [&out](const TypeInfo::ImageSampler& image_sampler) {
                out << "sampler2D";
            },
        },
        type.data
    );
    return out;
}

// Vastly simplified version of the structure of TypeInfo. We can resolve everything except
// nested arrays with just an Identifier.
struct TypeRef {
    Identifier name;
    std::vector<int32_t> array_sizes;
    Opt<Ref<TypeInfo>> resolved_type;
};

struct TypedIdentifier {
    Identifier name;
    TypeRef type;
};

struct PipelineParameter {
    Identifier name;
    Identifier value;
};

struct Expr {
    struct Call {
        Identifier name;
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

    struct NumberLiteral {
        double value;
        SourceLocation location;
    };

    struct VariableAccess {
        Identifier name;
    };

    std::variant<NumberLiteral, Call, Binary, Unary, ListAccess, FieldAccess, VariableAccess>
        data;

    Expr(Identifier identifier) : data(VariableAccess{ identifier }) {}
    Expr(double number, SourceLocation location) : data(NumberLiteral{ number, location }) {}
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

    template <typename T> const T& get() const {
        return std::get<T>(data);
    }

    SourceLocation get_location() const {
        if (is<VariableAccess>()) {
            return get<VariableAccess>().name.location;
        } else if (is<NumberLiteral>()) {
            return get<NumberLiteral>().location;
        } else if (is<Call>()) {
            return get<Call>().name.location;
        } else if (is<Binary>()) {
            return get<Binary>().lhs->get_location();
        } else if (is<Unary>()) {
            return get<Unary>().expr->get_location();
        } else if (is<ListAccess>()) {
            return get<ListAccess>().list->get_location();
        } else if (is<FieldAccess>()) {
            return get<FieldAccess>().object->get_location();
        }
        assert(false);
    }
};

struct Stmt {
    struct Return {
        uint8_t _unused;
    };

    struct Var {
        TypedIdentifier typedIdentifier;
        Opt<Ref<Expr>> expr;
    };

    struct ExprStmt {
        Ref<Expr> expr;
    };

    struct IfStmt {
        Ref<Expr> condition;
        std::vector<Ref<Stmt>> then_branch;
        Opt<std::vector<Ref<Stmt>>> else_branch;
    };

    struct For {
        PoolStr iterator_name;
        Ref<Expr> start;
        Ref<Expr> end;
        Opt<Ref<Expr>> step;
        std::vector<Ref<Stmt>> body;
    };

    struct Break {
        uint8_t _unused;
    };

    struct Continue {
        uint8_t _unused;
    };

    std::variant<Return, Var, ExprStmt, IfStmt, For, Break, Continue> data;

    Stmt(Return return_) : data(return_) {}
    Stmt(Var var) : data(var) {}
    Stmt(Ref<Expr> expr) : data(ExprStmt{ expr }) {}
    Stmt(
        Ref<Expr> condition,
        std::vector<Ref<Stmt>> then_branch,
        Opt<std::vector<Ref<Stmt>>> else_branch
    )
        : data(IfStmt{ condition, then_branch, else_branch }) {}
    Stmt(PoolStr iterator_name, Ref<Expr> start, Ref<Expr> end, Opt<Ref<Expr>> step, std::vector<Ref<Stmt>> body)
        : data(For{ iterator_name, start, end, step, body }) {}
    Stmt(Break break_) : data(break_) {}
    Stmt(Continue continue_) : data(continue_) {}

    template <typename T> bool is() const {
        return std::holds_alternative<T>(data);
    }

    template <typename T> T& get() {
        return std::get<T>(data);
    }

    template <typename T> const T& get() const {
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
        Opt<Ref<TypeInfo>> resolved_type;
        bool is_interface;
    };

    struct Function {
        Identifier name;
        std::vector<TypedIdentifier> params;
        std::vector<TypedIdentifier> rets;
        std::vector<Ref<Stmt>> stmts;
        uint32_t return_type_id;
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
            out << member.type.name.name.c_str() << " " << member.name.name.c_str() << ",\n";
        }
        indent--;
        print_indent();
        out << "}\n";
    }

    void print(const Decl::Function& function) {
        out << colorize::magenta("function ") << function.name.name.c_str() << "(";
        for (size_t i = 0; i < function.params.size(); i++) {
            const TypedIdentifier& param = function.params[i];
            out << param.type.name.name.c_str() << " " << param.name.name.c_str();
            if (i + 1 < function.params.size()) {
                out << ", ";
            }
        }
        out << ") -> (";
        for (size_t i = 0; i < function.rets.size(); i++) {
            const TypedIdentifier& ret = function.rets[i];
            out << ret.type.name.name.c_str() << " " << ret.name.name.c_str();
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
                [this](const Stmt::ExprStmt& expr) {
                    print_expr(*expr.expr);
                },
                [this](const Stmt::IfStmt& ifStmt) {
                    print(ifStmt);
                },
                [this](const Stmt::For& forStmt) {
                    print(forStmt);
                },
                [this](const Stmt::Continue& continue_) {
                    print(continue_);
                },
                [this](const Stmt::Break& break_) {
                    print(break_);
                },
            },
            stmt.data
        );
    }

    void print(const Stmt::Return&) {
        out << colorize::blue("return");
    }

    void print(const Stmt::Var& var) {
        out << colorize::bright_blue(var.typedIdentifier.type.name.name.c_str()) << " "
            << var.typedIdentifier.name.name.c_str();
        if (var.expr) {
            out << " = ";
            print_expr(**var.expr);
        }
    }

    void print(const Stmt::IfStmt& ifStmt) {
        out << colorize::blue("if") << " (";
        print_expr(*ifStmt.condition);
        out << ") {\n";
        indent++;
        for (const Ref<Stmt>& stmt : ifStmt.then_branch) {
            print_indent();
            print(*stmt);
            out << "\n";
        }
        indent--;
        print_indent();
        out << "}";
        if (ifStmt.else_branch) {
            out << " " << colorize::blue("else") << " {\n";
            indent++;
            for (const Ref<Stmt>& stmt : *ifStmt.else_branch) {
                print_indent();
                print(*stmt);
                out << "\n";
            }
            indent--;
            print_indent();
            out << "}";
        }
    }

    void print(const Stmt::For& forStmt) {
        out << colorize::blue("for") << " " << forStmt.iterator_name.c_str() << " = ";
        print_expr(*forStmt.start);
        out << ":";
        print_expr(*forStmt.end);
        if (forStmt.step.has_value()) {
            out << ":";
            print_expr(*forStmt.step.value());
        }

        out << " {\n";
        indent++;
        for (const Ref<Stmt>& stmt : forStmt.body) {
            print_indent();
            print(*stmt);
            out << "\n";
        }
        indent--;
        print_indent();
        out << "}";
    }

    void print(const Stmt::Continue&) {
        out << colorize::blue("continue");
    }

    void print(const Stmt::Break&) {
        out << colorize::blue("break");
    }

    void print_expr(const Expr& expr) {
        std::visit(
            overloaded{
                [this](const Expr::VariableAccess& identifier) {
                    out << identifier.name.name.c_str();
                },
                [this](const Expr::NumberLiteral& number) {
                    out << number.value;
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
        out << call.name.name.c_str() << "(";
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

enum class StorageClass : uint32_t {
    Input = spv::StorageClassInput,
    Output = spv::StorageClassOutput,
    Uniform = spv::StorageClassUniform,
    ImageSampler = spv::StorageClassUniformConstant,
    StorageBuffer = spv::StorageClassStorageBuffer,
    Function = spv::StorageClassFunction,
};
