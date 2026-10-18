#include "ast/ast.hpp"

#include <stdexcept>

namespace lab::ast {
const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::Integer: return "Integer";
    case Kind::String: return "String";
    case Kind::Boolean: return "Boolean";
    case Kind::Name: return "Name";
    case Kind::Unary: return "Unary";
    case Kind::Binary: return "Binary";
    case Kind::Call: return "Call";
    case Kind::Cast: return "Cast";
    case Kind::Index: return "Index";
    case Kind::Member: return "Member";
    case Kind::Array: return "Array";
    case Kind::NewArray: return "NewArray";
    case Kind::ParallelMap: return "ParallelMap";
    case Kind::Block: return "Block";
    case Kind::Variable: return "Variable";
    case Kind::Assignment: return "Assignment";
    case Kind::Expression: return "Expression";
    case Kind::If: return "If";
    case Kind::While: return "While";
    case Kind::For: return "For";
    case Kind::RangeFor: return "RangeFor";
    case Kind::Return: return "Return";
    case Kind::Empty: return "Empty";
    case Kind::Function: return "Function";
    case Kind::Struct: return "Struct";
    }
    throw std::logic_error("unknown AST node kind");
}
} // namespace lab::ast
