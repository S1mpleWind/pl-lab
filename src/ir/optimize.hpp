#pragma once
#include <string>
namespace lab::ir {
// LLVM scalar passes plus checked-integer runtime constant folding.
std::string optimize(const std::string &llvm);
} // namespace lab::ir
