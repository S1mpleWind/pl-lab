#pragma once

#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace lab {

struct SourcePosition {
    std::string file;
    std::size_t offset = 0; // Byte offsets and columns; lines/columns start at 1.
    std::size_t line = 1;
    std::size_t column = 1;
};

struct SourceSpan {
    SourcePosition begin;
    SourcePosition end; // Exclusive.
};

inline std::ostream& operator<<(std::ostream& out, const SourcePosition& position) {
    return out << position.file << ':' << position.line << ':' << position.column;
}

inline std::ostream& operator<<(std::ostream& out, const SourceSpan& span) {
    return out << span.begin;
}

class DiagnosticError : public std::runtime_error {
public:
    DiagnosticError(SourceSpan where, std::string category, std::string message)
        : std::runtime_error(std::move(message)), span(std::move(where)),
          category(std::move(category)) {}
    SourceSpan span;
    std::string category;
};

} // namespace lab

