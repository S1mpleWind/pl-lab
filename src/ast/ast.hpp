#pragma once

#include "ast/source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lab::ast {

enum class Kind {
    Integer, Float, String, Boolean, Name, Unary, Binary, Call, Cast, Index, Member,
    Array, NewArray, ParallelMap, Block, Variable, Assignment, Expression,
    If, While, For, RangeFor, Return, Empty, Function, Struct
};

const char* kind_name(Kind kind);

struct Node {
    Node(Kind node_kind, SourceSpan where) : kind(node_kind), span(std::move(where)) {}
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Kind kind;
    SourceSpan span;
};

struct TypeRef {
    std::string name;
    bool array = false;
    SourceSpan span;
};

struct Expr : Node { using Node::Node; };
struct Stmt : Node { using Node::Node; };
struct Decl : Node { using Node::Node; };
// unique_ptr 表示树形独占所有权；语义旁表只借用节点地址，不负责释放。
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;
using Expressions = std::vector<ExprPtr>;
using Statements = std::vector<StmtPtr>;
using Declarations = std::vector<DeclPtr>;

struct IntegerExpr final : Expr {
    explicit IntegerExpr(SourceSpan span) : Expr(Kind::Integer, std::move(span)) {}
    // Preserve magnitude so unary '-' can represent INT64_MIN; sema checks range.
    std::string magnitude;
};
struct FloatExpr final : Expr {
    explicit FloatExpr(SourceSpan span) : Expr(Kind::Float, std::move(span)) {}
    std::string literal; // Preserve decimal spelling; sema validates binary64 range.
};
struct StringExpr final : Expr {
    explicit StringExpr(SourceSpan span) : Expr(Kind::String, std::move(span)) {}
    std::string value; // Decoded ASCII bytes, including embedded NUL.
};
struct BooleanExpr final : Expr {
    explicit BooleanExpr(SourceSpan span) : Expr(Kind::Boolean, std::move(span)) {}
    bool value = false;
};
struct NameExpr final : Expr {
    explicit NameExpr(SourceSpan span) : Expr(Kind::Name, std::move(span)) {}
    std::string name;
};
struct UnaryExpr final : Expr {
    explicit UnaryExpr(SourceSpan span) : Expr(Kind::Unary, std::move(span)) {}
    std::string op;
    ExprPtr operand;
};
struct BinaryExpr final : Expr {
    explicit BinaryExpr(SourceSpan span) : Expr(Kind::Binary, std::move(span)) {}
    std::string op;
    ExprPtr left;
    ExprPtr right;
};
struct CallExpr final : Expr {
    explicit CallExpr(SourceSpan span) : Expr(Kind::Call, std::move(span)) {}
    ExprPtr callee;
    Expressions arguments;
};
struct CastExpr final : Expr {
    explicit CastExpr(SourceSpan span) : Expr(Kind::Cast, std::move(span)) {}
    TypeRef type;
    ExprPtr value;
};
struct IndexExpr final : Expr {
    explicit IndexExpr(SourceSpan span) : Expr(Kind::Index, std::move(span)) {}
    ExprPtr base;
    ExprPtr index;
};
struct MemberExpr final : Expr {
    explicit MemberExpr(SourceSpan span) : Expr(Kind::Member, std::move(span)) {}
    ExprPtr base;
    std::string member;
};
struct ArrayExpr final : Expr {
    explicit ArrayExpr(SourceSpan span) : Expr(Kind::Array, std::move(span)) {}
    Expressions elements;
};
struct NewArrayExpr final : Expr {
    explicit NewArrayExpr(SourceSpan span) : Expr(Kind::NewArray, std::move(span)) {}
    TypeRef element_type;
    ExprPtr length;
};
struct BlockStmt final : Stmt {
    explicit BlockStmt(SourceSpan span) : Stmt(Kind::Block, std::move(span)) {}
    Statements statements;
};
struct ParallelMapExpr final : Expr {
    explicit ParallelMapExpr(SourceSpan span) : Expr(Kind::ParallelMap, std::move(span)) {}
    std::string index_name;
    SourceSpan index_span;
    std::string element_name;
    SourceSpan element_span;
    ExprPtr input;
    std::unique_ptr<BlockStmt> body;
    ExprPtr result; // One trailing yield, separate from ordinary statements.
    SourceSpan yield_span;
};
struct VariableStmt final : Stmt {
    explicit VariableStmt(SourceSpan span) : Stmt(Kind::Variable, std::move(span)) {}
    std::string name;
    SourceSpan name_span;
    TypeRef type;
    ExprPtr initializer; // Null means uninitialized; sema must check reads.
};
struct AssignmentStmt final : Stmt {
    explicit AssignmentStmt(SourceSpan span) : Stmt(Kind::Assignment, std::move(span)) {}
    ExprPtr target; // Assignability is checked by sema.
    ExprPtr value;
};
struct ExpressionStmt final : Stmt {
    explicit ExpressionStmt(SourceSpan span) : Stmt(Kind::Expression, std::move(span)) {}
    ExprPtr expression;
};
struct IfBranch {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> body;
};
struct IfStmt final : Stmt {
    explicit IfStmt(SourceSpan span) : Stmt(Kind::If, std::move(span)) {}
    std::vector<IfBranch> branches;
    std::unique_ptr<BlockStmt> else_body;
};
struct WhileStmt final : Stmt {
    explicit WhileStmt(SourceSpan span) : Stmt(Kind::While, std::move(span)) {}
    ExprPtr condition;
    std::unique_ptr<BlockStmt> body;
};
struct ForStmt final : Stmt {
    explicit ForStmt(SourceSpan span) : Stmt(Kind::For, std::move(span)) {}
    StmtPtr initializer;
    ExprPtr condition;
    StmtPtr step;
    std::unique_ptr<BlockStmt> body;
};
struct RangeForStmt final : Stmt {
    explicit RangeForStmt(SourceSpan span) : Stmt(Kind::RangeFor, std::move(span)) {}
    std::string name;
    SourceSpan name_span;
    TypeRef type;
    ExprPtr iterable;
    std::unique_ptr<BlockStmt> body;
};
struct ReturnStmt final : Stmt {
    explicit ReturnStmt(SourceSpan span) : Stmt(Kind::Return, std::move(span)) {}
    ExprPtr value;
};
struct EmptyStmt final : Stmt {
    explicit EmptyStmt(SourceSpan span) : Stmt(Kind::Empty, std::move(span)) {}
};
struct Parameter {
    std::string name;
    SourceSpan name_span;
    TypeRef type;
    SourceSpan span;
};
struct FunctionDecl final : Decl {
    explicit FunctionDecl(SourceSpan span) : Decl(Kind::Function, std::move(span)) {}
    std::string name;
    SourceSpan name_span;
    std::vector<Parameter> parameters;
    TypeRef return_type;
    std::unique_ptr<BlockStmt> body; // Null for a function prototype ending in ';'.
};
struct Field {
    std::string name;
    SourceSpan name_span;
    TypeRef type;
    SourceSpan span;
};
struct StructDecl final : Decl {
    explicit StructDecl(SourceSpan span) : Decl(Kind::Struct, std::move(span)) {}
    std::string name;
    SourceSpan name_span;
    std::vector<Field> fields;
};
struct SourceFile {
    std::string path;
    SourceSpan span;
    Declarations declarations;
};
struct Program {
    std::vector<SourceFile> files; // Preserve command-line source order.
};

} // namespace lab::ast
