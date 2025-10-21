#pragma once

#include "repr.hpp"
#include <variant>

struct ReprWalker {
  public:
    virtual void visit(TypedIdentifier&) {}

    virtual void visit(Decl& decl) {
        std::visit(
            [this](auto& decl) {
                this->visit(decl);
            },
            decl.data
        );
    }

    virtual void visit(Decl::Import&) {}
    virtual void visit(Decl::Function& function) {
        for (auto& param : function.params) {
            visit(param);
        }

        for (auto& ret : function.rets) {
            visit(ret);
        }

        for (auto& stmt : function.stmts) {
            visit(*stmt);
        }
    }
    virtual void visit(Decl::Struct& struct_) {
        for (auto& field : struct_.members) {
            visit(field);
        }
    }
    virtual void visit(Decl::Pipeline& pipeline) {
        for (auto& stage : pipeline.params) {
            visit(stage);
        }
    }
    virtual void visit(PipelineParameter&) {}

    virtual void visit(Stmt& stmt) {
        std::visit(
            [this](auto& stmt) {
                this->visit(stmt);
            },
            stmt.data
        );
    }

    virtual void visit(Stmt::Var& var) {
        visit(var.typedIdentifier);
        if (var.expr) {
            visit(**var.expr);
        }
    }
    virtual void visit(Stmt::Return&) {}

    virtual void visit(Stmt::ExprStmt& exprStmt) {
        visit(*exprStmt.expr);
    }

    virtual void visit(Expr& expr) {
        std::visit(
            [this](auto& expr) {
                this->visit(expr);
            },
            expr.data
        );
    }

    virtual void visit(Expr::Binary& binary) {
        visit(*binary.lhs);
        visit(*binary.rhs);
    }

    virtual void visit(Expr::Unary& unary) {
        visit(*unary.expr);
    }

    virtual void visit(Expr::Call& call) {
        for (auto& arg : call.args) {
            visit(*arg);
        }
    }

    virtual void visit(Expr::ListAccess& listAccess) {
        visit(*listAccess.list);
        visit(*listAccess.index);
    }

    virtual void visit(Expr::FieldAccess& fieldAccess) {
        visit(*fieldAccess.object);
    }

    virtual void visit(Expr::NumberLiteral&) {}
    virtual void visit(Expr::VariableAccess&) {}
};
