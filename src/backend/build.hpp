#pragma once
#include <filesystem>
#include <string>
namespace lab::backend {
void build(const std::string &llvm, const std::filesystem::path &output,
           const std::filesystem::path &runtime);
}
