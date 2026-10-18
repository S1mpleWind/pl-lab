#pragma once
#include "ast/ast.hpp"
#include <ostream>
#include <string_view>

namespace lab::ast {
void write_json_string(std::ostream& out, std::string_view value);
void dump_json(std::ostream& out, const Program& program, bool locations = true);
} // namespace lab::ast
