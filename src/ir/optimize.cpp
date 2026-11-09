#include "ir/optimize.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace lab::ir {
namespace {
using Integer = std::int64_t;
using Constant = std::optional<Integer>;
// A state contains only facts proved constant. Missing entries mean unknown.
// Stack-cell facts use a separate namespace, so pointers never become integers.
using State = std::map<std::string, Integer>;
enum class Kind {
    Alloca,
    Load,
    Store,
    Call,
    Binary,
    Compare,
    Extend,
    Phi,
    Jump,
    Branch,
    Return,
    Other
};
struct Instruction {
    std::string text, result, operation, callee;
    Kind kind = Kind::Other;
    std::vector<std::string> operands;
    std::vector<std::string> targets;
    Constant constant;
    bool boolean = false;
};
struct Block {
    std::string name;
    std::vector<Instruction> instructions;
};
struct Function {
    std::string header, name;
    std::vector<Block> blocks;
};
struct Module {
    std::vector<std::string> prefix;
    std::vector<Function> functions;
};
std::string trim(const std::string &value) {
    auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
std::vector<std::string> matches(const std::string &text, const std::regex &pattern,
                                 int group = 0) {
    std::vector<std::string> values;
    for (std::sregex_iterator i(text.begin(), text.end(), pattern), end; i != end; ++i)
        values.push_back((*i)[group]);
    return values;
}
Instruction parse_instruction(std::string text) {
    Instruction instruction;
    instruction.text = trim(text);
    std::string rhs = instruction.text;
    auto equal = rhs.find(" = ");
    if (equal != std::string::npos) {
        instruction.result = rhs.substr(0, equal);
        rhs = rhs.substr(equal + 3);
    }
    static const std::regex local(R"(%[-a-zA-Z$._0-9]+)");
    static const std::regex typed(R"(i(?:64|1) (%[-a-zA-Z$._0-9]+|-?[0-9]+|true|false))");
    static const std::regex target(R"(label %([-a-zA-Z$._0-9]+))");
    static const std::regex incoming(R"(\[ ([^,]+), %([-a-zA-Z$._0-9]+) \])");
    if (rhs.rfind("alloca ", 0) == 0)
        instruction.kind = Kind::Alloca;
    else if (rhs.rfind("load ", 0) == 0) {
        instruction.kind = Kind::Load;
        instruction.operands = matches(rhs, local);
    } else if (rhs.rfind("store ", 0) == 0) {
        instruction.kind = Kind::Store;
        instruction.operands = matches(rhs, typed, 1);
        auto pointers = matches(rhs, local);
        instruction.operands.push_back(pointers.back());
    } else if (rhs.rfind("call ", 0) == 0) {
        instruction.kind = Kind::Call;
        auto at = rhs.find('@'), end = rhs.find('(', at);
        if (at == std::string::npos || end == std::string::npos)
            throw std::runtime_error("unsupported IR call");
        instruction.callee = rhs.substr(at + 1, end - at - 1);
        instruction.operands = matches(rhs.substr(end), typed, 1);
    } else if (rhs.rfind("icmp ", 0) == 0) {
        instruction.kind = Kind::Compare;
        instruction.boolean = true;
        std::istringstream words(rhs);
        std::string opcode, type, left, right;
        words >> opcode >> instruction.operation >> type >> left >> right;
        left.pop_back();
        instruction.operands = {left, right};
    } else if (rhs.rfind("xor ", 0) == 0 || rhs.rfind("add ", 0) == 0) {
        instruction.kind = Kind::Binary;
        std::istringstream words(rhs);
        std::string type, left, right;
        words >> instruction.operation >> type >> left >> right;
        left.pop_back();
        instruction.boolean = type == "i1";
        instruction.operands = {left, right};
    } else if (rhs.rfind("zext ", 0) == 0) {
        instruction.kind = Kind::Extend;
        instruction.operands = matches(rhs, typed, 1);
    } else if (rhs.rfind("phi ", 0) == 0) {
        instruction.kind = Kind::Phi;
        for (std::sregex_iterator i(rhs.begin(), rhs.end(), incoming), end; i != end; ++i) {
            instruction.operands.push_back(trim((*i)[1]));
            instruction.targets.push_back((*i)[2]);
        }
    } else if (rhs.rfind("br ", 0) == 0) {
        instruction.targets = matches(rhs, target, 1);
        if (instruction.targets.size() == 1)
            instruction.kind = Kind::Jump;
        else if (instruction.targets.size() == 2) {
            instruction.kind = Kind::Branch;
            instruction.operands = matches(rhs, typed, 1);
        } else
            throw std::runtime_error("unsupported IR branch");
    } else if (rhs.rfind("ret ", 0) == 0) {
        instruction.kind = Kind::Return;
        instruction.operands = matches(rhs, typed, 1);
    }
    return instruction;
}
Module parse(const std::string &text) {
    Module module;
    std::istringstream lines(text);
    std::string line;
    Function *function = nullptr;
    Block *block = nullptr;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.rfind("define ", 0) == 0) {
            module.functions.emplace_back();
            function = &module.functions.back();
            function->header = line;
            auto at = line.find('@');
            function->name = line.substr(at + 1, line.find('(', at) - at - 1);
            block = nullptr;
        } else if (function && line == "}") {
            function = nullptr;
            block = nullptr;
        } else if (function && !line.empty()) {
            if (line.back() == ':') {
                function->blocks.push_back({line.substr(0, line.size() - 1), {}});
                block = &function->blocks.back();
            } else {
                if (!block)
                    throw std::runtime_error("IR instruction outside a basic block");
                block->instructions.push_back(parse_instruction(line));
            }
        } else if (!function && !line.empty())
            module.prefix.push_back(line);
    }
    return module;
}
std::string render(const Module &module) {
    std::ostringstream out;
    for (const auto &line : module.prefix)
        out << line << '\n';
    for (const auto &function : module.functions) {
        out << function.header << '\n';
        for (const auto &block : function.blocks) {
            out << block.name << ":\n";
            for (const auto &instruction : block.instructions)
                out << "  " << instruction.text << '\n';
        }
        out << "}\n";
    }
    return out.str();
}
Constant value(const std::string &operand, const State &state) {
    if (operand == "true")
        return 1;
    if (operand == "false")
        return 0;
    if (!operand.empty() && operand[0] != '%') {
        try {
            std::size_t used = 0;
            auto result = std::stoll(operand, &used);
            if (used == operand.size())
                return result;
        } catch (const std::exception &) {
        }
        return {};
    }
    auto found = state.find(operand);
    return found == state.end() ? Constant{} : Constant{found->second};
}
Constant arithmetic(Integer op, Integer a, Integer b) {
    Integer result = 0;
    switch (op) {
    case 0:
        if (__builtin_add_overflow(a, b, &result))
            return {};
        break;
    case 1:
        if (__builtin_sub_overflow(a, b, &result))
            return {};
        break;
    case 2:
        if (__builtin_mul_overflow(a, b, &result))
            return {};
        break;
    case 3:
    case 4:
        if (b == 0 || (a == std::numeric_limits<Integer>::min() && b == -1))
            return {};
        result = op == 3 ? a / b : a % b;
        break;
    default:
        return {};
    }
    return result;
}
using Edge = std::pair<std::string, std::string>;
struct Flow {
    std::map<std::string, State> output;
    std::set<Edge> edges;
    std::set<std::string> reachable;
};
void intersect(State &left, const State &right) {
    for (auto item = left.begin(); item != left.end();) {
        auto found = right.find(item->first);
        if (found == right.end() || found->second != item->second)
            item = left.erase(item);
        else
            ++item;
    }
}
Constant evaluate(const Instruction &instruction, const State &state, const Flow &flow,
                  const std::string &block) {
    std::vector<Integer> arguments;
    if (instruction.kind == Kind::Phi) {
        Constant result;
        for (std::size_t i = 0; i < instruction.targets.size(); ++i) {
            const auto &predecessor = instruction.targets[i];
            if (!flow.edges.count({predecessor, block}))
                continue;
            auto incoming = value(instruction.operands[i], flow.output.at(predecessor));
            if (!incoming)
                return {};
            if (result && *result != *incoming)
                return {};
            result = incoming;
        }
        return result;
    }
    for (const auto &operand : instruction.operands) {
        auto constant = value(operand, state);
        if (!constant)
            return {};
        arguments.push_back(*constant);
    }
    if (instruction.kind == Kind::Call) {
        if (instruction.callee == "lab_int_op" && arguments.size() == 3)
            return arithmetic(arguments[0], arguments[1], arguments[2]);
        if (instruction.callee == "lab_int_neg" && arguments.size() == 1 &&
            arguments[0] != std::numeric_limits<Integer>::min())
            return -arguments[0];
    }
    if (instruction.kind == Kind::Extend && !arguments.empty())
        return arguments[0];
    if (instruction.kind == Kind::Binary && arguments.size() == 2) {
        if (instruction.operation == "xor")
            return arguments[0] ^ arguments[1];
        // LLVM add has wrapping semantics. Source-language checked arithmetic is a call.
        return static_cast<Integer>(static_cast<std::uint64_t>(arguments[0]) +
                                    static_cast<std::uint64_t>(arguments[1]));
    }
    if (instruction.kind == Kind::Compare && arguments.size() == 2) {
        auto a = arguments[0], b = arguments[1];
        const auto &op = instruction.operation;
        if (op == "eq")
            return a == b;
        if (op == "ne")
            return a != b;
        if (op == "slt")
            return a < b;
        if (op == "sle")
            return a <= b;
        if (op == "sgt")
            return a > b;
        if (op == "sge")
            return a >= b;
    }
    return {};
}
std::set<std::string> trackable_slots(const Function &function) {
    std::set<std::string> slots;
    for (const auto &block : function.blocks)
        for (const auto &instruction : block.instructions)
            if (instruction.kind == Kind::Alloca)
                slots.insert(instruction.result);
    // A slot is tracked only when its address never escapes via calls or other instructions.
    for (const auto &block : function.blocks)
        for (const auto &instruction : block.instructions) {
            if (instruction.kind == Kind::Alloca || instruction.kind == Kind::Load ||
                instruction.kind == Kind::Store)
                continue;
            for (const auto &reference :
                 matches(instruction.text, std::regex(R"(%[-a-zA-Z$._0-9]+)")))
                slots.erase(reference);
        }
    return slots;
}
State transfer(Block &block, State state, const Flow &flow, const std::set<std::string> &slots) {
    for (auto &instruction : block.instructions) {
        instruction.constant = {};
        if (instruction.kind == Kind::Store) {
            const auto &slot = instruction.operands.back();
            if (slots.count(slot)) {
                auto constant = value(instruction.operands.front(), state);
                if (constant)
                    state["!" + slot] = *constant;
                else
                    state.erase("!" + slot);
            }
        } else if (instruction.kind == Kind::Load && slots.count(instruction.operands.front())) {
            auto found = state.find("!" + instruction.operands.front());
            if (found != state.end())
                instruction.constant = found->second;
        } else
            instruction.constant = evaluate(instruction, state, flow, block.name);
        if (!instruction.result.empty()) {
            if (instruction.constant)
                state[instruction.result] = *instruction.constant;
            else
                state.erase(instruction.result);
        }
    }
    return state;
}
Flow analyze(Function &function) {
    Flow flow;
    if (function.blocks.empty())
        return flow;
    const auto entry = function.blocks.front().name;
    flow.reachable.insert(entry);
    auto slots = trackable_slots(function);
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &block : function.blocks) {
            if (!flow.reachable.count(block.name))
                continue;
            State input;
            bool first = true;
            if (block.name == entry)
                first = false;
            for (const auto &edge : flow.edges)
                if (edge.second == block.name) {
                    if (first) {
                        input = flow.output.at(edge.first);
                        first = false;
                    } else
                        intersect(input, flow.output.at(edge.first));
                }
            auto output = transfer(block, std::move(input), flow, slots);
            auto found = flow.output.find(block.name);
            if (found == flow.output.end() || found->second != output) {
                flow.output[block.name] = output;
                changed = true;
            }
            if (block.instructions.empty())
                continue;
            const auto &terminator = block.instructions.back();
            auto targets = terminator.targets;
            if (terminator.kind == Kind::Branch) {
                auto condition = value(terminator.operands.front(), output);
                if (condition)
                    targets = {targets[*condition ? 0 : 1]};
            }
            for (const auto &target : targets) {
                changed = flow.edges.insert({block.name, target}).second || changed;
                changed = flow.reachable.insert(target).second || changed;
            }
        }
    }
    return flow;
}
std::string substitute(const std::string &text, const State &constants) {
    static const std::regex local(R"(%[-a-zA-Z$._0-9]+)");
    std::string result;
    std::size_t previous = 0;
    for (std::sregex_iterator i(text.begin(), text.end(), local), end; i != end; ++i) {
        result += text.substr(previous, i->position() - previous);
        auto found = constants.find(i->str());
        result += found == constants.end() ? i->str() : std::to_string(found->second);
        previous = i->position() + i->length();
    }
    return result + text.substr(previous);
}
void propagate_and_prune(Function &function) {
    auto flow = analyze(function);
    State constants;
    for (const auto &block : function.blocks)
        if (flow.reachable.count(block.name))
            for (const auto &instruction : block.instructions)
                if (instruction.constant && !instruction.result.empty())
                    constants[instruction.result] = *instruction.constant;
    for (auto &block : function.blocks)
        if (flow.reachable.count(block.name)) {
            for (auto &instruction : block.instructions) {
                if (instruction.kind == Kind::Branch) {
                    auto condition = value(instruction.operands.front(), constants);
                    if (condition)
                        instruction = parse_instruction("br label %" +
                                                        instruction.targets[*condition ? 0 : 1]);
                } else if (instruction.constant && !instruction.result.empty()) {
                    instruction = parse_instruction(instruction.result + " = add " +
                                                    (instruction.boolean ? "i1" : "i64") + " 0, " +
                                                    std::to_string(*instruction.constant));
                }
                auto equal = instruction.text.find(" = ");
                std::string prefix =
                    equal == std::string::npos ? "" : instruction.text.substr(0, equal + 3);
                auto rhs = equal == std::string::npos ? instruction.text
                                                      : instruction.text.substr(equal + 3);
                instruction = parse_instruction(prefix + substitute(rhs, constants));
            }
        }
    // Recompute reachability after pruning edges, then repair phi predecessor lists.
    std::set<std::string> reachable;
    if (!function.blocks.empty())
        reachable.insert(function.blocks.front().name);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &block : function.blocks)
            if (reachable.count(block.name) && !block.instructions.empty())
                for (const auto &target : block.instructions.back().targets)
                    changed = reachable.insert(target).second || changed;
    }
    function.blocks.erase(
        std::remove_if(function.blocks.begin(), function.blocks.end(),
                       [&](const Block &block) { return !reachable.count(block.name); }),
        function.blocks.end());
    std::set<Edge> edges;
    for (const auto &block : function.blocks)
        if (!block.instructions.empty())
            for (const auto &target : block.instructions.back().targets)
                edges.insert({block.name, target});
    for (auto &block : function.blocks)
        for (auto &instruction : block.instructions)
            if (instruction.kind == Kind::Phi) {
                std::string text = instruction.result + " = phi i64 ";
                bool first = true;
                for (std::size_t i = 0; i < instruction.targets.size(); ++i)
                    if (edges.count({instruction.targets[i], block.name})) {
                        if (!first)
                            text += ", ";
                        first = false;
                        text +=
                            "[ " + instruction.operands[i] + ", %" + instruction.targets[i] + " ]";
                    }
                if (first)
                    throw std::runtime_error("reachable phi without predecessor");
                instruction = parse_instruction(text);
            }
}
// Effects are explicitly classified and propagated over the user-function call graph.
// Traps, allocation failure and nontermination are observable even with unused results.
enum Effect { IO = 1, HeapWrite = 2, Allocate = 4, Trap = 8, Diverge = 16, Unknown = 31 };
using Effects = std::map<std::string, unsigned>;
unsigned runtime_effect(const std::string &name) {
    if (name == "lab_equal" || name == "lab_strlen" || name == "lab_length")
        return 0;
    if (name == "lab_print" || name == "lab_read_char")
        return IO | Trap;
    if (name == "lab_set")
        return HeapWrite | Trap;
    if (name == "lab_int_op" || name == "lab_int_neg" || name == "lab_get" ||
        name == "lab_float_op" || name == "lab_float_neg" || name == "lab_float_to_int" ||
        name == "lab_to_int")
        return Trap;
    if (name == "lab_int_to_float")
        return 0;
    if (name == "lab_parallel")
        return Unknown;
    if (name.rfind("lab_", 0) == 0)
        return Allocate | Trap;
    return Unknown;
}
Effects analyze_effects(const Module &module) {
    Effects effects;
    std::map<std::string, std::set<std::string>> calls;
    for (const auto &function : module.functions)
        effects[function.name] = 0;
    for (const auto &function : module.functions) {
        // Conservatively regard cyclic CFGs as potentially nonterminating.
        std::set<std::string> visited, active;
        std::map<std::string, std::vector<std::string>> successors;
        for (const auto &block : function.blocks)
            if (!block.instructions.empty())
                successors[block.name] = block.instructions.back().targets;
        const auto cycle = [&](auto &&self, const std::string &block) -> bool {
            if (active.count(block))
                return true;
            if (!visited.insert(block).second)
                return false;
            active.insert(block);
            for (const auto &target : successors[block])
                if (self(self, target))
                    return true;
            active.erase(block);
            return false;
        };
        if (!function.blocks.empty() && cycle(cycle, function.blocks.front().name))
            effects[function.name] |= Diverge;
        for (const auto &block : function.blocks)
            for (const auto &instruction : block.instructions) {
                if (instruction.kind == Kind::Other)
                    effects[function.name] |= Unknown;
                if (instruction.kind == Kind::Call) {
                    if (effects.count(instruction.callee))
                        calls[function.name].insert(instruction.callee);
                    else
                        effects[function.name] |= runtime_effect(instruction.callee);
                }
            }
    }
    for (const auto &function : module.functions) {
        std::set<std::string> visited;
        const auto recursive = [&](auto &&self, const std::string &name) -> bool {
            if (!visited.insert(name).second)
                return false;
            for (const auto &callee : calls[name])
                if (callee == function.name || self(self, callee))
                    return true;
            return false;
        };
        if (recursive(recursive, function.name))
            effects[function.name] |= Diverge;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &item : calls)
            for (const auto &callee : item.second) {
                auto combined = effects[item.first] | effects[callee];
                if (combined != effects[item.first]) {
                    effects[item.first] = combined;
                    changed = true;
                }
            }
    }
    return effects;
}
void eliminate_dead_code(Function &function, const Effects &effects) {
    static const std::regex local(R"(%[-a-zA-Z$._0-9]+)");
    auto slots = trackable_slots(function);
    std::set<std::string> live_values, live_slots;
    std::set<const Instruction *> live;
    const auto mark = [&](const Instruction &instruction) {
        bool changed = live.insert(&instruction).second;
        auto equal = instruction.text.find(" = ");
        auto rhs =
            equal == std::string::npos ? instruction.text : instruction.text.substr(equal + 3);
        for (const auto &reference : matches(rhs, local))
            live_values.insert(reference);
        if (instruction.kind == Kind::Load)
            live_slots.insert(instruction.operands.front());
        return changed;
    };
    for (const auto &block : function.blocks)
        for (const auto &instruction : block.instructions) {
            bool root = instruction.kind == Kind::Jump || instruction.kind == Kind::Branch ||
                        instruction.kind == Kind::Return;
            if (instruction.kind == Kind::Call) {
                auto found = effects.find(instruction.callee);
                root = (found == effects.end() ? runtime_effect(instruction.callee)
                                               : found->second) != 0;
            }
            if (instruction.kind == Kind::Store && !slots.count(instruction.operands.back()))
                root = true;
            if (instruction.kind == Kind::Other)
                root = true;
            if (root)
                mark(instruction);
        }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &block : function.blocks)
            for (const auto &instruction : block.instructions)
                if ((!instruction.result.empty() && live_values.count(instruction.result)) ||
                    (instruction.kind == Kind::Store &&
                     live_slots.count(instruction.operands.back())))
                    changed = mark(instruction) || changed;
    }
    for (auto &block : function.blocks) {
        std::vector<Instruction> kept;
        for (auto &instruction : block.instructions)
            if (live.count(&instruction))
                kept.push_back(std::move(instruction));
        block.instructions = std::move(kept);
    }
}
void merge_blocks(Function &function) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, std::set<std::string>> predecessors;
        for (const auto &block : function.blocks)
            if (!block.instructions.empty())
                for (const auto &target : block.instructions.back().targets)
                    predecessors[target].insert(block.name);
        for (std::size_t i = 0; i < function.blocks.size() && !changed; ++i) {
            auto &block = function.blocks[i];
            if (block.instructions.empty() || block.instructions.back().kind != Kind::Jump)
                continue;
            auto target = block.instructions.back().targets.front();
            if (target == block.name || target == function.blocks.front().name ||
                predecessors[target].size() != 1)
                continue;
            auto found = std::find_if(function.blocks.begin(), function.blocks.end(),
                                      [&](const Block &item) { return item.name == target; });
            if (found == function.blocks.end() ||
                (!found->instructions.empty() && found->instructions.front().kind == Kind::Phi))
                continue;
            auto replacement = block.name;
            block.instructions.pop_back();
            block.instructions.insert(block.instructions.end(), found->instructions.begin(),
                                      found->instructions.end());
            for (auto &successor : function.blocks)
                for (auto &instruction : successor.instructions)
                    if (instruction.kind == Kind::Phi) {
                        std::string text = instruction.result + " = phi i64 ";
                        for (std::size_t n = 0; n < instruction.targets.size(); ++n) {
                            if (n)
                                text += ", ";
                            auto label = instruction.targets[n] == target ? replacement
                                                                          : instruction.targets[n];
                            text += "[ " + instruction.operands[n] + ", %" + label + " ]";
                        }
                        instruction = parse_instruction(text);
                    }
            function.blocks.erase(found);
            changed = true;
        }
    }
}
// LLVM is used only as an IR verifier, with no optimization passes.
void verify(const std::string &text) {
    auto pattern = (std::filesystem::temp_directory_path() / "lab-verify-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back(0);
    auto raw = mkdtemp(buffer.data());
    if (!raw)
        throw std::runtime_error("cannot create verifier workspace");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{raw};
    auto input = cleanup.path / "module.ll";
    {
        std::ofstream stream(input);
        stream << text;
        stream.close();
        if (!stream)
            throw std::runtime_error("cannot write verifier input");
    }
    auto configured = std::getenv("LAB_OPT");
    std::string executable = configured ? configured : "opt";
    auto pid = fork();
    if (pid < 0)
        throw std::runtime_error("cannot start IR verifier");
    if (pid == 0) {
        execlp(executable.c_str(), executable.c_str(), "-passes=verify", "-disable-output",
               input.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            throw std::runtime_error("cannot wait for IR verifier");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("IR verification failed; check LAB_OPT and LLVM version");
}
std::string escape(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
            out += "\\l";
        else
            out += c;
    }
    return out;
}
} // namespace
std::string optimize(const std::string &llvm) {
    verify(llvm);
    auto module = parse(llvm);
    for (;;) {
        auto before = render(module);
        for (auto &function : module.functions)
            propagate_and_prune(function);
        auto effects = analyze_effects(module);
        for (auto &function : module.functions) {
            eliminate_dead_code(function, effects);
            merge_blocks(function);
        }
        auto after = render(module);
        if (after == before) {
            verify(after);
            return after;
        }
    }
}
std::string control_flow_graph(const std::string &llvm) {
    auto module = parse(llvm);
    std::ostringstream out;
    out << "digraph module {\n  node [shape=box];\n";
    for (const auto &function : module.functions) {
        out << "  subgraph \"cluster_" << escape(function.name) << "\" {\n    label=\""
            << escape(function.name) << "\";\n";
        for (const auto &block : function.blocks) {
            auto id = function.name + "/" + block.name;
            std::string label = block.name + ":\n";
            for (const auto &instruction : block.instructions)
                label += instruction.text + "\n";
            out << "    \"" << escape(id) << "\" [label=\"" << escape(label) << "\"];\n";
            if (!block.instructions.empty())
                for (std::size_t n = 0; n < block.instructions.back().targets.size(); ++n) {
                    out << "    \"" << escape(id) << "\" -> \""
                        << escape(function.name + "/" + block.instructions.back().targets[n])
                        << "\"";
                    if (block.instructions.back().kind == Kind::Branch)
                        out << " [label=\"" << (n == 0 ? "true" : "false") << "\"]";
                    out << ";\n";
                }
        }
        out << "  }\n";
    }
    out << "}\n";
    return out.str();
}
} // namespace lab::ir
