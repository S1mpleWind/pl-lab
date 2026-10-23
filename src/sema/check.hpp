#pragma once
#include "ast/ast.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace lab::sema {
struct Type {
    std::string name;
    bool array = false;
    bool operator==(const Type &b) const { return name == b.name && array == b.array; }
    bool operator!=(const Type &b) const { return !(*this == b); }
    std::string str() const { return name + (array ? "[]" : ""); }
    bool composite() const {
        return array || (name != "int" && name != "float" && name != "bool" && name != "string" &&
                         name != "void");
    }
};
struct Binding {
    int id;
    Type type;
    bool initialized;
    bool readonly;
};
// AST 地址是旁表键：Checked 不能比原 Program 活得更久，也不能换一棵树复用。
// 函数表仅保存有函数体的定义；原型仅在检查时参与签名和顺序验证。
struct Checked {
    std::unordered_map<const ast::Expr *, Type> types;
    std::set<const ast::Expr *> promotions; // Checked int -> float conversions.
    std::unordered_map<const ast::Node *, int> bindings;
    std::map<int, Type> variables;
    std::map<const ast::FunctionDecl *, std::vector<int>> parameters;
    std::map<std::string, const ast::FunctionDecl *> functions;
    std::map<std::string, const ast::StructDecl *> structures;
    std::unordered_map<const ast::ParallelMapExpr *, std::vector<int>> captures;
    std::unordered_map<const ast::ParallelMapExpr *, std::pair<int, int>> map_bindings;
};
Checked check(const ast::Program &program);
} // namespace lab::sema
