#pragma once
#include <string>
namespace lab::ir {
// Dataflow and transformations for the LLVM subset emitted by this compiler.
std::string optimize(const std::string &llvm);
std::string control_flow_graph(const std::string &llvm);
} // namespace lab::ir
