#include "lesl/arena.hpp"
#include "lesl/ref_container.hpp"
#include "lesl/repr.hpp"
#include "lesl/repr_walker.hpp"

namespace lesl {
struct TypeInfoFiller : public ReprWalker {
    CompilationArena& arena;
    TypeInfoFiller(CompilationArena& arena) : arena(arena) {}

    using ReprWalker::visit;

    void visit(TypedIdentifier& typedIdentifier) override {
        ReprWalker::visit(typedIdentifier);

        typedIdentifier.type.resolved_type = create_or_get_info(arena, typedIdentifier.type);
    }

    void visit(Decl::StructMember& member) override {
        ReprWalker::visit(member);

        member.type.resolved_type = create_or_get_info(arena, member.type);
    }

    void visit(Decl::Struct& struct_) override {
        ReprWalker::visit(struct_);

        struct_.resolved_type = create_or_get_info(
            arena,
            TypeRef{
                struct_.name,
                {},
                {},
            }
        );
    }

    Ref<TypeInfo> create_or_get_info(CompilationArena& arena, const TypeRef& type) {
        TypeInfo info;
        bool filled = false;

        if (type.array_sizes.size() > 0) {
            TypeRef base_type = type;
            base_type.array_sizes.pop_back();
            Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

            int32_t size = type.array_sizes[type.array_sizes.size() - 1];

            info = TypeInfo::create_array(
                arena.string_pool,
                underlying_info,
                size != -1,
                size == -1 ? 0 : size
            );
            filled = true;
        } else {
            // search structs
            for (auto decl : arena.decls) {
                if (decl->is<Decl::Struct>()) {
                    if (decl->get<Decl::Struct>().name.name == type.name.name) {
                        std::vector<TypeInfo::Struct::Member> members;
                        for (auto& member : decl->get<Decl::Struct>().members) {
                            members.push_back(
                                {
                                    member.name.name,
                                    create_or_get_info(arena, member.type),
                                    member.interpolation,
                                }
                            );
                        }

                        info = TypeInfo::create_struct(type.name.name, members);
                        filled = true;
                        break;
                    }
                }
            }
            if (!filled) {
                // not a struct or array, try builtins
                std::string name = type.name.name.to_string();
                bool is_vector = false;
                bool is_matrix = false;
                uint32_t vector_size = 0;
                uint32_t matrix_columns = 0;
                uint32_t matrix_rows = 0;

                // extract matrix/vector sizes
                if (std::isdigit(name[name.size() - 1])) {
                    if (name.size() >= 3 && std::isdigit(name[name.size() - 3]) &&
                        name[name.size() - 2] == 'x') {
                        matrix_columns = name[name.size() - 1] - '0';
                        matrix_rows = name[name.size() - 3] - '0';

                        is_matrix = true;

                        // base type
                        name = name.substr(0, name.size() - 3) + std::to_string(matrix_rows);

                    } else {
                        vector_size = name[name.size() - 1] - '0';
                        is_vector = true;

                        name = name.substr(0, name.size() - 1);
                    }
                }

                if (is_vector) {
                    TypeRef base_type = TypeRef{
                        Identifier{ arena.string_pool.add(name), type.name.location },
                        {},
                        {},
                    };

                    Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

                    info = TypeInfo::create_vector(
                        arena.string_pool,
                        underlying_info,
                        vector_size
                    );
                    filled = true;
                } else if (is_matrix) {
                    TypeRef base_type = TypeRef{
                        Identifier{ arena.string_pool.add(name), type.name.location },
                        {},
                        {},
                    };

                    Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

                    info = TypeInfo::create_matrix(
                        arena.string_pool,
                        underlying_info,
                        matrix_columns
                    );
                    filled = true;
                } else {
                    TypeInfo::BuiltinPrimitive primitive = TypeInfo::BuiltinPrimitive::Void;
                    if (name == "void") {
                        primitive = TypeInfo::BuiltinPrimitive::Void;
                    } else if (name == "bool") {
                        primitive = TypeInfo::BuiltinPrimitive::Bool;
                    } else if (name == "int") {
                        primitive = TypeInfo::BuiltinPrimitive::Int;
                    } else if (name == "uint") {
                        primitive = TypeInfo::BuiltinPrimitive::Uint;
                    } else if (name == "float") {
                        primitive = TypeInfo::BuiltinPrimitive::Float;
                    }

                    info = TypeInfo::create_primitive(arena.string_pool, primitive);
                    filled = true;
                }
            }
        }

        assert(filled);

        for (const Ref<TypeInfo>& t : arena.types) {
            if (*t == info) {
                return t;
            }
        }

        Ref<TypeInfo> new_info = arena.alloc(std::move(info));
        return new_info;
    }
};
}; // namespace lesl
