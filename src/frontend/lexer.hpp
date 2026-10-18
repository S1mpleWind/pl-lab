#pragma once

#include "frontend/parser.hpp"

#include <string>
#include <string_view>

namespace lab::frontend {

struct LexerState {
    explicit LexerState(std::string filename) { position.file = std::move(filename); }
    void advance(const char* text, std::size_t size);
    SourcePosition position;
    SourceSpan token;
    SourceSpan opening;
    std::string literal;
    bool previous_cr = false;
};

class Lexer {
public:
    Lexer(std::string filename, std::string_view source);
    ~Lexer();
    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;
    Parser::symbol_type next();
    const SourcePosition& position() const { return state_.position; }

private:
    LexerState state_;
    void* scanner_ = nullptr;
    void* buffer_ = nullptr;
};

} // namespace lab::frontend

lab::frontend::Parser::symbol_type lab_scan_token(void* scanner);

