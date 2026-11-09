#include "ir/optimize.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace lab::ir {
namespace {
struct Workspace {
    std::filesystem::path path;
    Workspace() {
        auto pattern = (std::filesystem::temp_directory_path() / "lab-opt-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back(0);
        auto directory = mkdtemp(buffer.data());
        if (!directory)
            throw std::runtime_error("cannot create optimizer workspace");
        path = directory;
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
void run_opt(const std::filesystem::path &input, const std::filesystem::path &output) {
    const char *configured = std::getenv("LAB_OPT");
    std::string executable = configured ? configured : "opt";
    auto pid = fork();
    if (pid < 0)
        throw std::runtime_error("cannot start LLVM optimizer");
    if (pid == 0) {
        execlp(executable.c_str(), executable.c_str(), "-S",
               "-passes=mem2reg,sccp,simplifycfg,adce", "-verify-each", input.c_str(), "-o",
               output.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            throw std::runtime_error("cannot wait for LLVM optimizer");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(
            "LLVM optimizer failed; check opt availability and LLVM version (LAB_OPT)");
}
std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read optimized IR");
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad())
        throw std::runtime_error("cannot read optimized IR");
    return text.str();
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    output.close();
    if (!output)
        throw std::runtime_error("cannot write optimizer input");
}
using Integer = std::int64_t;
std::optional<Integer> evaluate(Integer op, Integer a, Integer b) {
    Integer result = 0;
    // Overflow/zero division must remain runtime errors, even for unused results.
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
std::pair<std::string, bool> fold(const std::string &input) {
    // Restrict matching to complete instructions produced by our emitter/opt.
    // Never infer purity for arbitrary user calls, allocations, I/O or float operations.
    static const std::regex operation(
        R"(^([ \t]*%[-a-zA-Z$._0-9]+ = )(?:tail )?call i64 @lab_int_op\(i64 ([0-4]), i64 (-?[0-9]+), i64 (-?[0-9]+)\)[ \t]*$)");
    static const std::regex negation(
        R"(^([ \t]*%[-a-zA-Z$._0-9]+ = )(?:tail )?call i64 @lab_int_neg\(i64 (-?[0-9]+)\)[ \t]*$)");
    std::istringstream lines(input);
    std::ostringstream result;
    std::string line;
    bool changed = false;
    while (std::getline(lines, line)) {
        std::smatch match;
        std::optional<Integer> value;
        std::string prefix;
        if (std::regex_match(line, match, operation)) {
            prefix = match[1];
            value = evaluate(std::stoll(match[2]), std::stoll(match[3]), std::stoll(match[4]));
        } else if (std::regex_match(line, match, negation)) {
            prefix = match[1];
            auto operand = std::stoll(match[2]);
            if (operand != std::numeric_limits<Integer>::min())
                value = -operand;
        }
        if (value) {
            line = prefix + "add i64 0, " + std::to_string(*value);
            changed = true;
        }
        result << line << '\n';
    }
    return {result.str(), changed};
}
} // namespace
std::string optimize(const std::string &llvm) {
    Workspace workspace;
    auto input = workspace.path / "input.ll", output = workspace.path / "output.ll";
    std::string current = llvm;
    // SCCP exposes constants in helper arguments. Each successful fold removes
    // at least one helper call, so this fixed point terminates without a round cap.
    for (;;) {
        write(input, current);
        run_opt(input, output);
        current = read(output);
        auto folded = fold(current);
        if (!folded.second)
            return current;
        current = std::move(folded.first);
    }
}
} // namespace lab::ir
