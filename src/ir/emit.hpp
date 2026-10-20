#pragma once
#include "sema/check.hpp"
#include <string>
namespace lab::ir {
std::string emit(const ast::Program &, const sema::Checked &);
}
