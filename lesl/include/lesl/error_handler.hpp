#pragma once

#include "lesl/token.hpp"
#include "lesl/unit.hpp"
#include "lesl/colorize.hpp"

#include <ostream>
#include <vector>
#include <cassert>

#define ENABLE_ASSERT_ON_ERROR 0

enum class ErrorType {
    UnexpectedCharacter,
    UnexpectedToken,
    UnknownType,
    FunctionRedefinition,
    StructRedefinition,
    StructMemberRedefinition,
    UnknownSizedArrayNotLast,
    PipelineRedefinition,
    MissingPipelineParameter,
    InvalidArraySize,
    InvalidVectorSize,
    InvalidMatrixSize,
    InvalidCompoundBaseType,
    RuntimeSizedArrayInStruct,
    InvalidFunctionCall,
    UnknownVariable,
    UnknownFunction,

    IncompatibleTypes,
    InvalidAssignment,
    InvalidCompositeArithmetic,
    InvalidLogicalType,
    InvalidEqualityComparison,
    InvalidRelationalComparison,
    InvalidBinaryOperation,
    InvalidUnaryOperation,
    InvalidAccess,
    InvalidVectorSwizzle,
    InvalidMatrixAccess,
    InvalidCall,
    InvalidVariableAssignment,
    InvalidArrayIndex,
    InvalidVectorIndex,
    InvalidArrayAccess,

    BadCallArgumentCount,
    BadCallArgument,
    BadCallArguments,
    BadPackedInputType,
    BadPackedInputPrimitiveType,
    BadPackedInput,
    BadVectorPrimitive,
    BadVectorSize,
    BadVectorInputType,
    BadVectorInputInconsistent,

    ConditionNotBool,
    BreakOutsideLoop,
    ContinueOutsideLoop,
};

struct ErrorData {
    bool non_empty;
    union {
        char character;
        TokenType token;
        PoolStr str;
    };

    ErrorData(char character) : non_empty(true), character(character) {}
    ErrorData(TokenType token) : non_empty(true), token(token) {}
    ErrorData(PoolStr str) : non_empty(true), str(str) {}

    ErrorData() : non_empty(false), character('0') {}
};

struct Error {
    ErrorType type;
    ErrorData data;
    SourceLocation location;

    void dump(std::ostream& out) const {
        out << colorize::bold(colorize::bright_red("ERROR")) << ": ";
        switch (type) {
            case ErrorType::UnexpectedCharacter:
                out << "Unexpected character '" << colorize::bold(data.character) << "'";
                break;
            case ErrorType::UnexpectedToken:
                out << "Unexpected token " << colorize::bold(data.token);
                break;
            case ErrorType::UnknownType:
                out << "Unknown type " << colorize::bold(data.str.to_string());
                break;
            case ErrorType::FunctionRedefinition:
                out << "Function " << colorize::bold(data.str.to_string())
                    << " was already defined";
                break;
            case ErrorType::StructRedefinition:
                out << "Struct " << colorize::bold(data.str.to_string())
                    << " was already defined";
                break;
            case ErrorType::StructMemberRedefinition:
                out << "Struct member " << colorize::bold(data.str.to_string())
                    << " was already defined";
                break;
            case ErrorType::UnknownSizedArrayNotLast:
                out << "Zero-sized array " << colorize::bold(data.str.to_string())
                    << " is not the last member of the struct";
                break;
            case ErrorType::PipelineRedefinition:
                out << "Pipeline " << colorize::bold(data.str.to_string())
                    << " was already defined";
                break;
            case ErrorType::MissingPipelineParameter:
                out << "Pipeline is missing parameter " << colorize::bold(data.str.to_string());
                break;
            case ErrorType::InvalidArraySize:
                out << "Invalid array size";
                break;
            case ErrorType::InvalidVectorSize:
                out << "Invalid vector size";
                break;
            case ErrorType::InvalidMatrixSize:
                out << "Invalid matrix size";
                break;
            case ErrorType::InvalidCompoundBaseType:
                out << "Invalid base type " << colorize::bold(data.str.to_string())
                    << "for a compound type. Only basic types 'float', 'int' and 'uint' "
                       "allowed";
                break;
            case ErrorType::RuntimeSizedArrayInStruct:
                out << "Runtime sized array is not allowed in struct "
                    << colorize::bold(data.str.to_string())
                    << " as it is not an interface (input or output)";
                break;
            case ErrorType::InvalidFunctionCall:
                out << "Invalid function call";
                break;
            case ErrorType::UnknownVariable:
                out << "Unknown variable " << colorize::bold(data.str.to_string());
                break;
            case ErrorType::UnknownFunction:
                out << "Unknown function " << colorize::bold(data.str.to_string());
                break;

            case ErrorType::IncompatibleTypes:
                out << "Incompatible types in operation";
                break;
            case ErrorType::InvalidAssignment:
                out << "Expression cannot be assigned to";
                break;
            case ErrorType::InvalidCompositeArithmetic:
                out << "Invalid arithmetic operation on composite types";
                break;
            case ErrorType::InvalidLogicalType:
                out << "Types cannot be used in logical operations";
                break;
            case ErrorType::InvalidEqualityComparison:
                out << "Types cannot be compared for equality or inequality";
                break;
            case ErrorType::InvalidRelationalComparison:
                out << "Types cannot be compared for relational operations";
                break;
            case ErrorType::InvalidBinaryOperation:
                out << "Invalid binary operation";
                break;
            case ErrorType::InvalidUnaryOperation:
                out << "Invalid unary operation";
            case ErrorType::InvalidAccess:
                out << "Invalid access to a composite type";
                break;
            case ErrorType::InvalidVectorSwizzle:
                out << "Invalid vector swizzle";
                break;
            case ErrorType::InvalidMatrixAccess:
                out << "Invalid matrix access";
                break;
            case ErrorType::InvalidCall:
                out << "Invalid function call";
                break;
            case ErrorType::InvalidVariableAssignment:
                out << "Variable " << colorize::bold(data.str.to_string())
                    << " cannot be assigned to";
                break;
            case ErrorType::InvalidArrayIndex:
                out << "Invalid array index";
                break;
            case ErrorType::InvalidVectorIndex:
                out << "Invalid vector index";
                break;
            case ErrorType::InvalidArrayAccess:
                out << "This expression cannot be accessed as an array";
                break;
            case ErrorType::BadCallArgumentCount:
                out << "Function parameter count does not match call argument count";
                break;
            case ErrorType::BadCallArgument:
                out << "Call argument for parameter " << colorize::bold(data.str.to_string())
                    << " has wrong type";
                break;
            case ErrorType::BadCallArguments:
                out << "Function call arguments don't match any overload";
                break;
            case ErrorType::BadPackedInputType:
                out << "Function requires packed input, which only accepts vector and "
                       "primitive arguments";
                break;
            case ErrorType::BadPackedInputPrimitiveType:
                out << "Function doesn't accept this primitive as component of packed call";
                break;
            case ErrorType::BadPackedInput:
                out << "Component count for packed input is the wrong size";
                break;
            case ErrorType::BadVectorPrimitive:
                out << "Function does not accept primitives or vectors with the underlying "
                    << colorize::bold(data.str.to_string()) << " type";
                break;
            case ErrorType::BadVectorSize:
                out << "Function call argument vector is the wrong size";
                break;
            case ErrorType::BadVectorInputType:
                    out << "Function only accepts primitive and vector types, "
                        << colorize::bold(data.str.to_string()) << " is not one of them";
                break;
            case ErrorType::BadVectorInputInconsistent:
                out << "Function call argumentrs must all be the same type";
                break;
            case ErrorType::ConditionNotBool:
                out << "Condition expression must be of type bool";
                break;
            case ErrorType::BreakOutsideLoop:
                out << "Break statement used outside of a loop";
                break;
            case ErrorType::ContinueOutsideLoop:
                out << "Continue statement used outside of a loop";
                break;
        }

        out << " at " << location.line << ":" << location.column << "\n";
    }
};

struct ErrorHandler {
    inline void error(ErrorType type, char character, SourceLocation location) {
#if ENABLE_ASSERT_ON_ERROR
        assert(false);
#endif
        errors.push_back({ type, { character }, location });
    }

    inline void error(ErrorType type, TokenType token, SourceLocation location) {
#if ENABLE_ASSERT_ON_ERROR
        assert(false);
#endif
        errors.push_back({ type, { token }, location });
    }

    inline void error(ErrorType type, PoolStr str, SourceLocation location) {
#if ENABLE_ASSERT_ON_ERROR
        assert(false);
#endif
        errors.push_back({ type, { str }, location });
    }

    inline void error(ErrorType type, SourceLocation location) {
#if ENABLE_ASSERT_ON_ERROR
        assert(false);
#endif
        errors.push_back({ type, {}, location });
    }

    inline bool has_errors() const {
        return !errors.empty();
    }

    inline void dump(std::ostream& out) const {
        for (const Error& error : errors) {
            error.dump(out);
        }
    }

    std::vector<Error> errors;
};
