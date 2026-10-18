#include "frontend/parse.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace lab::frontend {

ast::SourceFile parse(std::string path, std::string_view source) {
    ParseContext context(path, source);
    Parser parser(context);
    if (parser.parse() != 0) throw std::runtime_error("parser failed without a diagnostic");
    SourcePosition begin;
    begin.file = path;
    return {std::move(path), {std::move(begin), context.lexer.position()},
            std::move(context.declarations)};
}

std::string read_source(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open source file: " + path);
    std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) throw std::runtime_error("cannot read source file: " + path);
    return source;
}

ast::Program parse_files(const std::vector<std::string>& paths) {
    ast::Program result;
    for (const auto& path : paths) {
        const auto source = read_source(path);
        result.files.push_back(parse(path, source));
    }
    return result;
}

std::vector<Token> tokenize(std::string path, std::string_view source) {
    Lexer lexer(std::move(path), source);
    std::vector<Token> result;
    for (;;) {
        auto symbol = lexer.next();
        Token token{symbol.name(), "", false, symbol.location};
        if (token.kind.size() >= 2 && token.kind.front() == '"' && token.kind.back() == '"')
            token.kind = token.kind.substr(1, token.kind.size() - 2);
        using K = Parser::symbol_kind;
        if (symbol.kind() == K::S_IDENTIFIER || symbol.kind() == K::S_INTEGER ||
            symbol.kind() == K::S_STRING_LITERAL) {
            token.value = symbol.value.as<std::string>();
            token.has_value = true;
        }
        const bool end = symbol.kind() == K::S_YYEOF;
        result.push_back(std::move(token));
        if (end) break;
    }
    return result;
}

} // namespace lab::frontend

