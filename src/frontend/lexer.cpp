#include "frontend/lexer.hpp"

#include <limits>
#include <stdexcept>

struct yy_buffer_state;
int lablex_init_extra(lab::frontend::LexerState*, void**);
int lablex_destroy(void*);
yy_buffer_state* lab_scan_bytes(const char*, int, void*);
void lab_delete_buffer(yy_buffer_state*, void*);

namespace lab::frontend {

void LexerState::advance(const char* text, std::size_t size) {
    token.begin = position;
    for (std::size_t i = 0; i < size; ++i) {
        const char ch = text[i];
        ++position.offset;
        if (ch == '\r') {
            ++position.line;
            position.column = 1;
            previous_cr = true;
        } else if (ch == '\n') {
            if (!previous_cr) ++position.line;
            position.column = 1;
            previous_cr = false;
        } else {
            ++position.column;
            previous_cr = false;
        }
    }
    token.end = position;
}

Lexer::Lexer(std::string filename, std::string_view source) : state_(std::move(filename)) {
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("source file exceeds scanner input limit");
    if (lablex_init_extra(&state_, &scanner_) != 0)
        throw std::runtime_error("could not initialize scanner");
    buffer_ = lab_scan_bytes(source.data(), static_cast<int>(source.size()), scanner_);
    if (!buffer_) {
        lablex_destroy(scanner_);
        scanner_ = nullptr;
        throw std::runtime_error("could not allocate scanner buffer");
    }
}

Lexer::~Lexer() {
    if (buffer_) lab_delete_buffer(static_cast<yy_buffer_state*>(buffer_), scanner_);
    if (scanner_) lablex_destroy(scanner_);
}

Parser::symbol_type Lexer::next() { return lab_scan_token(scanner_); }

} // namespace lab::frontend

