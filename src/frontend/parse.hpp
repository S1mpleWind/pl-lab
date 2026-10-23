#pragma once

#include "frontend/lexer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lab::frontend {

struct Token {
    std::string kind;
    std::string value;
    bool has_value = false;
    SourceSpan span;
};

// 每次解析拥有独立 scanner 和声明容器；没有跨文件共享的词法状态。
class ParseContext {
public:
    ParseContext(std::string filename, std::string_view source)
        : lexer(std::move(filename), source) {}
    Parser::symbol_type next() { return lexer.next(); }
    Lexer lexer;
    ast::Declarations declarations;
};

ast::SourceFile parse(std::string path, std::string_view source);
ast::Program parse_files(const std::vector<std::string>& paths);
std::string read_source(const std::string& path);
std::vector<Token> tokenize(std::string path, std::string_view source);

} // namespace lab::frontend
