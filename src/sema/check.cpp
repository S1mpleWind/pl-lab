#include "sema/check.hpp"
#include <algorithm>
#include <functional>
#include <limits>

namespace lab::sema {
using namespace ast;
namespace {
const Type integer{"int"}, boolean{"bool"}, text{"string"}, nothing{"void"};
struct Checker {
    Checked out;
    std::vector<std::map<std::string, Binding>> scopes;
    std::map<std::string, std::set<std::string>> calls;
    std::set<std::string> io, parallel;
    std::vector<std::pair<std::string, SourceSpan>> parallel_calls;
    const FunctionDecl *function = nullptr;
    int next = 0, parallel_depth = 0;
    [[noreturn]] void fail(const Node &n, const std::string &s) {
        throw DiagnosticError(n.span, "type error", s);
    }
    [[noreturn]] void fail(const SourceSpan &s, const std::string &m) {
        throw DiagnosticError(s, "type error", m);
    }
    void need(const Node &n, Type actual, Type expected) {
        if (actual != expected)
            fail(n, "expected " + expected.str() + ", got " + actual.str());
    }
    Type type(const TypeRef &t, bool allow_void = false) {
        if (t.name != "int" && t.name != "bool" && t.name != "string" && t.name != "void" &&
            !out.structures.count(t.name))
            fail(t.span, "unknown type: " + t.name);
        if (t.name == "void" && (!allow_void || t.array))
            fail(t.span, "void is only valid as a function return type");
        return {t.name, t.array};
    }
    Binding &lookup(const std::string &name, const Node &n) {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto found = i->find(name);
            if (found != i->end())
                return found->second;
        }
        fail(n, "undeclared variable: " + name);
    }
    int declare(const std::string &name, Type t, bool initialized, bool readonly,
                const SourceSpan &span) {
        if (scopes.back().count(name))
            fail(span, "duplicate local declaration: " + name);
        int id = next++;
        scopes.back()[name] = {id, t, initialized, readonly};
        out.variables[id] = t;
        return id;
    }
    Type expr(const Expr &e, const Type *expected = nullptr) {
        Type result;
        switch (e.kind) {
        case Kind::Integer: {
            auto s = static_cast<const IntegerExpr &>(e).magnitude;
            auto p = s.find_first_not_of('0');
            s = p == std::string::npos ? "0" : s.substr(p);
            if (s.size() > 19 || (s.size() == 19 && s > "9223372036854775807"))
                fail(e, "integer literal out of int64 range");
            result = integer;
            break;
        }
        case Kind::String:
            result = text;
            break;
        case Kind::Boolean:
            result = boolean;
            break;
        case Kind::Name: {
            auto &b = lookup(static_cast<const NameExpr &>(e).name, e);
            if (!b.initialized)
                fail(e, "read of uninitialized variable");
            out.bindings[&e] = b.id;
            result = b.type;
            break;
        }
        case Kind::Unary: {
            auto &u = static_cast<const UnaryExpr &>(e);
            if (u.op == "-" && u.operand->kind == Kind::Integer) {
                auto s = static_cast<const IntegerExpr &>(*u.operand).magnitude;
                auto p = s.find_first_not_of('0');
                s = p == std::string::npos ? "0" : s.substr(p);
                if (s == "9223372036854775808") {
                    out.types[u.operand.get()] = integer;
                    result = integer;
                    break;
                }
            }
            result = u.op == "!" ? boolean : integer;
            need(e, expr(*u.operand), result);
            break;
        }
        case Kind::Binary: {
            auto &b = static_cast<const BinaryExpr &>(e);
            auto a = expr(*b.left), c = expr(*b.right);
            need(e, c, a);
            if (b.op == "&&" || b.op == "||") {
                need(e, a, boolean);
                result = boolean;
            } else if (b.op == "==" || b.op == "!=") {
                if (a != integer && a != boolean && a != text)
                    fail(e, "equality requires int, bool or string");
                result = boolean;
            } else {
                need(e, a, integer);
                result = (b.op == "<" || b.op == "<=" || b.op == ">" || b.op == ">=") ? boolean
                                                                                      : integer;
            }
            break;
        }
        case Kind::Cast: {
            auto &c = static_cast<const CastExpr &>(e);
            result = type(c.type);
            auto from = expr(*c.value);
            if (from != result &&
                !((from == text && result == integer) || (from == integer && result == text) ||
                  (from == boolean && result == text)))
                fail(e,
                     "unsupported explicit conversion from " + from.str() + " to " + result.str());
            break;
        }
        case Kind::Array: {
            auto &a = static_cast<const ArrayExpr &>(e);
            if (expected && expected->array)
                result = *expected;
            else if (!a.elements.empty()) {
                result = expr(*a.elements.front());
                if (result.array || result == nothing)
                    fail(e, "nested/void arrays are not supported");
                result.array = true;
            } else
                fail(e, "empty array requires an expected array type");
            Type element{result.name};
            for (auto &x : a.elements)
                need(*x, expr(*x, &element), element);
            break;
        }
        case Kind::NewArray: {
            auto &a = static_cast<const NewArrayExpr &>(e);
            result = type(a.element_type);
            result.array = true;
            need(e, expr(*a.length), integer);
            break;
        }
        case Kind::Index: {
            auto &i = static_cast<const IndexExpr &>(e);
            result = expr(*i.base);
            if (!result.array)
                fail(e, "indexing requires an array");
            result.array = false;
            need(e, expr(*i.index), integer);
            break;
        }
        case Kind::Member: {
            auto &m = static_cast<const MemberExpr &>(e);
            auto base = expr(*m.base);
            if (base.array || !out.structures.count(base.name))
                fail(e, "field access requires a struct");
            bool found = false;
            for (auto &f : out.structures.at(base.name)->fields)
                if (f.name == m.member) {
                    result = type(f.type);
                    found = true;
                    break;
                }
            if (!found)
                fail(e, "unknown field: " + m.member);
            break;
        }
        case Kind::Call: {
            auto &c = static_cast<const CallExpr &>(e);
            if (c.callee->kind != Kind::Name)
                fail(e, "callee must be a declared function or struct");
            auto name = static_cast<const NameExpr &>(*c.callee).name;
            for (auto i = scopes.rbegin(); i != scopes.rend(); ++i)
                if (i->count(name))
                    fail(e, "local variable is not callable: " + name);
            std::vector<Type> args;
            if (out.functions.count(name)) {
                auto f = out.functions.at(name);
                result = type(f->return_type, true);
                for (auto &p : f->parameters)
                    args.push_back(type(p.type));
                calls[function->name].insert(name);
                if (parallel_depth)
                    parallel_calls.push_back({name, e.span});
            } else if (out.structures.count(name)) {
                result = {name};
                for (auto &f : out.structures.at(name)->fields)
                    args.push_back(type(f.type));
            } else if (name == "print") {
                args = {text};
                result = nothing;
                io.insert(function->name);
                if (parallel_depth)
                    fail(e, "I/O is forbidden in parallel map");
            } else if (name == "read_char") {
                result = integer;
                io.insert(function->name);
                if (parallel_depth)
                    fail(e, "I/O is forbidden in parallel map");
            } else if (name == "concat") {
                args = {text, text};
                result = text;
            } else if (name == "substr") {
                args = {text, integer, integer};
                result = text;
            } else if (name == "len") {
                if (c.arguments.size() != 1)
                    fail(e, "len requires one argument");
                auto t = expr(*c.arguments[0]);
                if (t != text && !t.array)
                    fail(e, "len requires string or array");
                result = integer;
                break;
            } else if (name == "range") {
                if (c.arguments.empty() || c.arguments.size() > 3)
                    fail(e, "range takes one to three integer arguments");
                args.assign(c.arguments.size(), integer);
                result = {"int", true};
            } else
                fail(e, "unknown function: " + name);
            if (c.arguments.size() != args.size())
                fail(e, "wrong number of arguments to " + name);
            for (std::size_t i = 0; i < args.size(); ++i)
                need(*c.arguments[i], expr(*c.arguments[i], &args[i]), args[i]);
            break;
        }
        case Kind::ParallelMap: {
            auto &m = static_cast<const ParallelMapExpr &>(e);
            if (parallel_depth)
                fail(e, "nested parallel map is not supported");
            parallel.insert(function->name);
            Type input = expr(*m.input);
            if (!input.array)
                fail(e, "parallel map input must be an array");
            auto saved = scopes;
            std::vector<int> captures;
            for (auto &scope : scopes)
                for (auto &item : scope) {
                    if (item.second.initialized)
                        captures.push_back(item.second.id);
                    item.second.readonly = true;
                }
            out.captures[&m] = captures;
            scopes.emplace_back();
            ++parallel_depth;
            int i = declare(m.index_name, integer, true, true, m.index_span);
            int x = declare(m.element_name, {input.name}, true, true, m.element_span);
            out.map_bindings[&m] = {i, x};
            for (auto &s : m.body->statements)
                stmt(*s);
            Type element;
            const Type *hint = nullptr;
            if (expected && expected->array) {
                element = {expected->name};
                hint = &element;
            }
            result = expr(*m.result, hint);
            if (result.array || result == nothing)
                fail(e, "map result must be a non-void scalar or struct");
            result.array = true;
            --parallel_depth;
            scopes = std::move(saved);
            break;
        }
        default:
            fail(e, "unsupported expression");
        }
        out.types[&e] = result;
        return result;
    }
    // A write must be rooted in a variable, never a temporary or captured value.
    Type target(const Expr &e, bool require_initialized = true) {
        if (e.kind == Kind::Name) {
            auto &b = lookup(static_cast<const NameExpr &>(e).name, e);
            if (b.readonly)
                fail(e, "parallel input and captured bindings are read-only");
            if (require_initialized && !b.initialized)
                fail(e, "write through an uninitialized value");
            out.bindings[&e] = b.id;
            out.types[&e] = b.type;
            return b.type;
        }
        if (e.kind == Kind::Index) {
            auto &i = static_cast<const IndexExpr &>(e);
            auto t = target(*i.base);
            if (!t.array)
                fail(e, "indexed assignment requires an array");
            need(e, expr(*i.index), integer);
            t.array = false;
            out.types[&e] = t;
            return t;
        }
        if (e.kind == Kind::Member) {
            auto &m = static_cast<const MemberExpr &>(e);
            target(*m.base);
            return expr(e);
        }
        fail(e, "assignment target must be a variable, array element or struct field");
    }
    bool block(const BlockStmt &b) {
        scopes.emplace_back();
        bool returns = false;
        for (auto &s : b.statements) {
            if (returns) {
                auto saved = scopes;
                stmt(*s);
                scopes = std::move(saved);
            } else
                returns = stmt(*s);
        }
        scopes.pop_back();
        return returns;
    }
    bool stmt(const Stmt &s) {
        switch (s.kind) {
        case Kind::Block:
            return block(static_cast<const BlockStmt &>(s));
        case Kind::Empty:
            break;
        case Kind::Variable: {
            auto &v = static_cast<const VariableStmt &>(s);
            auto t = type(v.type);
            if (v.initializer)
                need(s, expr(*v.initializer, &t), t);
            out.bindings[&s] = declare(v.name, t, bool(v.initializer), false, v.name_span);
            break;
        }
        case Kind::Assignment: {
            auto &a = static_cast<const AssignmentStmt &>(s);
            auto t = target(*a.target, false);
            need(s, expr(*a.value, &t), t);
            if (a.target->kind == Kind::Name)
                lookup(static_cast<const NameExpr &>(*a.target).name, s).initialized = true;
            break;
        }
        case Kind::Expression:
            expr(*static_cast<const ExpressionStmt &>(s).expression);
            break;
        case Kind::Return: {
            if (parallel_depth)
                fail(s, "return is forbidden in parallel map");
            auto &r = static_cast<const ReturnStmt &>(s);
            auto t = type(function->return_type, true);
            need(s, r.value ? expr(*r.value, &t) : nothing, t);
            return true;
        }
        case Kind::If: {
            auto &i = static_cast<const IfStmt &>(s);
            auto before = scopes;
            std::vector<decltype(scopes)> continuing;
            bool all = true;
            for (auto &b : i.branches) {
                scopes = before;
                need(s, expr(*b.condition), boolean);
                bool r = block(*b.body);
                all = all && r;
                if (!r)
                    continuing.push_back(scopes);
            }
            scopes = before;
            if (i.else_body) {
                bool r = block(*i.else_body);
                all = all && r;
                if (!r)
                    continuing.push_back(scopes);
            } else {
                all = false;
                continuing.push_back(before);
            }
            scopes = before;
            for (std::size_t k = 0; k < scopes.size(); ++k)
                for (auto &p : scopes[k]) {
                    bool initialized = true;
                    for (auto &branch : continuing)
                        initialized = initialized && branch[k].at(p.first).initialized;
                    p.second.initialized = initialized;
                }
            return all;
        }
        case Kind::While: {
            auto &w = static_cast<const WhileStmt &>(s);
            need(s, expr(*w.condition), boolean);
            auto saved = scopes;
            block(*w.body);
            scopes = std::move(saved);
            break;
        }
        case Kind::For: {
            auto &f = static_cast<const ForStmt &>(s);
            scopes.emplace_back();
            if (f.initializer)
                stmt(*f.initializer);
            if (f.condition)
                need(s, expr(*f.condition), boolean);
            auto saved = scopes;
            bool returns = block(*f.body);
            if (f.step) {
                auto step_state = scopes;
                stmt(*f.step);
                scopes = std::move(step_state);
            }
            scopes = std::move(saved);
            scopes.pop_back();
            (void)returns;
            break;
        }
        case Kind::RangeFor: {
            auto &f = static_cast<const RangeForStmt &>(s);
            auto t = expr(*f.iterable);
            if (!t.array)
                fail(s, "for-in requires an array");
            t.array = false;
            auto declared = type(f.type);
            need(s, t, declared);
            auto saved = scopes;
            scopes.emplace_back();
            out.bindings[&s] = declare(f.name, t, true, false, f.name_span);
            block(*f.body);
            scopes = std::move(saved);
            break;
        }
        default:
            fail(s, "unsupported statement");
        }
        return false;
    }
    Checked run(const Program &p) {
        const std::set<std::string> reserved = {"print", "read_char", "concat", "substr", "len",
                                                "range", "int",       "bool",   "string", "void"};
        std::set<std::string> names = reserved;
        for (auto &file : p.files)
            for (auto &d : file.declarations) {
                std::string n = d->kind == Kind::Function
                                    ? static_cast<const FunctionDecl &>(*d).name
                                    : static_cast<const StructDecl &>(*d).name;
                if (!names.insert(n).second)
                    fail(*d, "duplicate or reserved declaration: " + n);
                if (d->kind == Kind::Function)
                    out.functions[n] = static_cast<const FunctionDecl *>(d.get());
                else
                    out.structures[n] = static_cast<const StructDecl *>(d.get());
            }
        // By-value struct layouts must be finite, including through array elements.
        std::set<std::string> visiting, done;
        std::function<void(const std::string &)> visit = [&](const std::string &n) {
            if (done.count(n))
                return;
            auto s = out.structures.at(n);
            if (!visiting.insert(n).second)
                fail(*s, "recursive struct layout is not supported");
            std::set<std::string> fields;
            for (auto &f : s->fields) {
                auto t = type(f.type);
                if (!fields.insert(f.name).second)
                    fail(f.span, "duplicate struct field");
                if (out.structures.count(t.name))
                    visit(t.name);
            }
            visiting.erase(n);
            done.insert(n);
        };
        for (auto &s : out.structures)
            visit(s.first);
        if (!out.functions.count("main")) {
            SourceSpan span;
            if (!p.files.empty())
                span = p.files[0].span;
            fail(span, "program requires def main() -> int or void");
        }
        auto main = out.functions.at("main");
        auto mt = type(main->return_type, true);
        if (!main->parameters.empty() || (mt != integer && mt != nothing))
            fail(*main, "main must have no parameters and return int or void");
        for (auto &entry : out.functions) {
            function = entry.second;
            out.parameters[function];
            scopes.clear();
            scopes.emplace_back();
            auto ret = type(function->return_type, true);
            for (auto &a : function->parameters)
                out.parameters[function].push_back(
                    declare(a.name, type(a.type), true, false, a.span));
            // Record parameter ids in declaration order via the initial scope.
            bool returns = block(*function->body);
            if (ret != nothing && !returns)
                fail(*function, "not all paths return a value");
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto &c : calls)
                for (auto &callee : c.second) {
                    if (io.count(callee))
                        changed = io.insert(c.first).second || changed;
                    if (parallel.count(callee))
                        changed = parallel.insert(c.first).second || changed;
                }
        }
        for (auto &c : parallel_calls) {
            if (io.count(c.first))
                fail(c.second, "parallel call may perform I/O: " + c.first);
            if (parallel.count(c.first))
                fail(c.second, "parallel call may start nested parallel work: " + c.first);
        }
        return std::move(out);
    }
};
} // namespace
Checked check(const ast::Program &p) {
    return Checker{}.run(p);
}
} // namespace lab::sema
