#include "ast/json.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lab::ast {

void write_json_string(std::ostream& out, std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    out.put('"');
    for (unsigned char byte : value) {
        switch (byte) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (byte < 0x20) out << "\\u00" << hex[byte >> 4] << hex[byte & 15];
            else out.put(static_cast<char>(byte));
        }
    }
    out.put('"');
}

namespace {
struct Json {
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;
    std::variant<std::nullptr_t, bool, std::size_t, std::string, Array, Object> value;
    Json() : value(nullptr) {}
    Json(const char* text) : value(std::string(text)) {}
    Json(std::string text) : value(std::move(text)) {}
    Json(bool boolean) : value(boolean) {}
    Json(std::size_t number) : value(number) {}
    Json(Array array) : value(std::move(array)) {}
    Json(Object object) : value(std::move(object)) {}
};
using Object = Json::Object;
using Array = Json::Array;

Json position(const SourcePosition& p) {
    return Object{{"file", p.file}, {"line", p.line}, {"column", p.column}, {"offset", p.offset}};
}
void span(Object& object, const SourceSpan& s, bool locations, const char* key = "span") {
    if (locations) object.emplace_back(key, Object{{"begin", position(s.begin)}, {"end", position(s.end)}});
}
Json type(const TypeRef& t, bool locations) {
    Object fields{{"name", t.name}, {"array", t.array}};
    span(fields, t.span, locations);
    return fields;
}
Json node(const Node& n, bool locations);
template<class T> Json child(const std::unique_ptr<T>& p, bool locations) {
    return p ? node(*p, locations) : Json{};
}
template<class T> Array children(const std::vector<std::unique_ptr<T>>& items, bool locations) {
    Array result;
    for (const auto& item : items) result.push_back(child(item, locations));
    return result;
}
Json node(const Node& n, bool locations) {
    Object f{{"kind", kind_name(n.kind)}};
    span(f, n.span, locations);
    switch (n.kind) {
    case Kind::Integer: f.emplace_back("magnitude", static_cast<const IntegerExpr&>(n).magnitude); break;
    case Kind::Float: f.emplace_back("literal", static_cast<const FloatExpr&>(n).literal); break;
    case Kind::String: f.emplace_back("value", static_cast<const StringExpr&>(n).value); break;
    case Kind::Boolean: f.emplace_back("value", static_cast<const BooleanExpr&>(n).value); break;
    case Kind::Name: f.emplace_back("name", static_cast<const NameExpr&>(n).name); break;
    case Kind::Unary: {
        const auto& x = static_cast<const UnaryExpr&>(n);
        f.emplace_back("operator", x.op);
        f.emplace_back("operand", child(x.operand, locations)); break;
    }
    case Kind::Binary: {
        const auto& x = static_cast<const BinaryExpr&>(n);
        f.emplace_back("operator", x.op);
        f.emplace_back("left", child(x.left, locations));
        f.emplace_back("right", child(x.right, locations)); break;
    }
    case Kind::Call: {
        const auto& x = static_cast<const CallExpr&>(n);
        f.emplace_back("callee", child(x.callee, locations));
        f.emplace_back("arguments", children(x.arguments, locations)); break;
    }
    case Kind::Cast: {
        const auto& x = static_cast<const CastExpr&>(n);
        f.emplace_back("type", type(x.type, locations));
        f.emplace_back("value", child(x.value, locations)); break;
    }
    case Kind::Index: {
        const auto& x = static_cast<const IndexExpr&>(n);
        f.emplace_back("base", child(x.base, locations));
        f.emplace_back("index", child(x.index, locations)); break;
    }
    case Kind::Member: {
        const auto& x = static_cast<const MemberExpr&>(n);
        f.emplace_back("base", child(x.base, locations));
        f.emplace_back("member", x.member); break;
    }
    case Kind::Array: f.emplace_back("elements", children(static_cast<const ArrayExpr&>(n).elements, locations)); break;
    case Kind::NewArray: {
        const auto& x = static_cast<const NewArrayExpr&>(n);
        f.emplace_back("element_type", type(x.element_type, locations));
        f.emplace_back("length", child(x.length, locations)); break;
    }
    case Kind::ParallelMap: {
        const auto& x = static_cast<const ParallelMapExpr&>(n);
        f.emplace_back("index_name", x.index_name);
        span(f, x.index_span, locations, "index_span");
        f.emplace_back("element_name", x.element_name);
        span(f, x.element_span, locations, "element_span");
        f.emplace_back("input", child(x.input, locations));
        f.emplace_back("body", child(x.body, locations));
        f.emplace_back("result", child(x.result, locations));
        span(f, x.yield_span, locations, "yield_span"); break;
    }
    case Kind::Block: f.emplace_back("statements", children(static_cast<const BlockStmt&>(n).statements, locations)); break;
    case Kind::Variable: {
        const auto& x = static_cast<const VariableStmt&>(n);
        f.emplace_back("name", x.name);
        span(f, x.name_span, locations, "name_span");
        f.emplace_back("type", type(x.type, locations));
        f.emplace_back("initializer", child(x.initializer, locations)); break;
    }
    case Kind::Assignment: {
        const auto& x = static_cast<const AssignmentStmt&>(n);
        f.emplace_back("target", child(x.target, locations));
        f.emplace_back("value", child(x.value, locations)); break;
    }
    case Kind::Expression: f.emplace_back("expression", child(static_cast<const ExpressionStmt&>(n).expression, locations)); break;
    case Kind::If: {
        const auto& x = static_cast<const IfStmt&>(n);
        Array branches;
        for (const auto& b : x.branches)
            branches.emplace_back(Object{{"condition", child(b.condition, locations)}, {"body", child(b.body, locations)}});
        f.emplace_back("branches", std::move(branches));
        f.emplace_back("else_body", child(x.else_body, locations)); break;
    }
    case Kind::While: {
        const auto& x = static_cast<const WhileStmt&>(n);
        f.emplace_back("condition", child(x.condition, locations));
        f.emplace_back("body", child(x.body, locations)); break;
    }
    case Kind::For: {
        const auto& x = static_cast<const ForStmt&>(n);
        f.emplace_back("initializer", child(x.initializer, locations));
        f.emplace_back("condition", child(x.condition, locations));
        f.emplace_back("step", child(x.step, locations));
        f.emplace_back("body", child(x.body, locations)); break;
    }
    case Kind::RangeFor: {
        const auto& x = static_cast<const RangeForStmt&>(n);
        f.emplace_back("name", x.name);
        span(f, x.name_span, locations, "name_span");
        f.emplace_back("type", type(x.type, locations));
        f.emplace_back("iterable", child(x.iterable, locations));
        f.emplace_back("body", child(x.body, locations)); break;
    }
    case Kind::Return: f.emplace_back("value", child(static_cast<const ReturnStmt&>(n).value, locations)); break;
    case Kind::Empty: break;
    case Kind::Function: {
        const auto& x = static_cast<const FunctionDecl&>(n);
        f.emplace_back("name", x.name);
        span(f, x.name_span, locations, "name_span");
        Array parameters;
        for (const auto& p : x.parameters) {
            Object item{{"name", p.name}, {"type", type(p.type, locations)}};
            span(item, p.name_span, locations, "name_span");
            span(item, p.span, locations);
            parameters.emplace_back(std::move(item));
        }
        f.emplace_back("parameters", std::move(parameters));
        f.emplace_back("return_type", type(x.return_type, locations));
        f.emplace_back("body", child(x.body, locations)); break;
    }
    case Kind::Struct: {
        const auto& x = static_cast<const StructDecl&>(n);
        f.emplace_back("name", x.name);
        span(f, x.name_span, locations, "name_span");
        Array fields;
        for (const auto& p : x.fields) {
            Object item{{"name", p.name}, {"type", type(p.type, locations)}};
            span(item, p.name_span, locations, "name_span");
            span(item, p.span, locations);
            fields.emplace_back(std::move(item));
        }
        f.emplace_back("fields", std::move(fields)); break;
    }
    }
    return f;
}

void write(std::ostream& out, const Json& json, std::size_t depth = 0) {
    auto indent = [&] { out << '\n' << std::string(depth * 2, ' '); };
    if (std::holds_alternative<std::nullptr_t>(json.value)) out << "null";
    else if (const auto* value = std::get_if<bool>(&json.value)) out << (*value ? "true" : "false");
    else if (const auto* value = std::get_if<std::size_t>(&json.value)) out << *value;
    else if (const auto* value = std::get_if<std::string>(&json.value)) write_json_string(out, *value);
    else if (const auto* array = std::get_if<Array>(&json.value)) {
        out << '[';
        for (std::size_t i = 0; i < array->size(); ++i) {
            if (i) out << ',';
            out << '\n' << std::string((depth + 1) * 2, ' ');
            write(out, (*array)[i], depth + 1);
        }
        if (!array->empty()) indent();
        out << ']';
    } else {
        const auto& object = std::get<Object>(json.value);
        out << '{';
        for (std::size_t i = 0; i < object.size(); ++i) {
            if (i) out << ',';
            out << '\n' << std::string((depth + 1) * 2, ' ');
            write_json_string(out, object[i].first);
            out << ": ";
            write(out, object[i].second, depth + 1);
        }
        if (!object.empty()) indent();
        out << '}';
    }
}
} // namespace

void dump_json(std::ostream& out, const Program& program, bool locations) {
    Array files;
    for (const auto& file : program.files) {
        Object fields{{"kind", "SourceFile"}, {"path", file.path},
                      {"declarations", children(file.declarations, locations)}};
        span(fields, file.span, locations);
        files.emplace_back(std::move(fields));
    }
    write(out, Object{{"kind", "Program"}, {"files", std::move(files)}});
    out << '\n';
}

} // namespace lab::ast
