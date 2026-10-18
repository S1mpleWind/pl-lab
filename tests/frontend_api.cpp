#include "ast/json.hpp"
#include "frontend/parse.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_context(int index) {
    const std::string filename = "thread-" + std::to_string(index) + ".toy";
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto file = lab::frontend::parse(filename, "def f() -> int { return 1 + 2; }");
        require(file.declarations.size() == 1, "missing function");
        require(file.declarations[0]->span.begin.file == filename, "scanner location leaked");
        auto* function = dynamic_cast<lab::ast::FunctionDecl*>(file.declarations[0].get());
        require(function && function->name == "f", "wrong function");
        auto* ret = dynamic_cast<lab::ast::ReturnStmt*>(function->body->statements[0].get());
        require(ret && ret->value->kind == lab::ast::Kind::Binary, "wrong expression");
        try {
            lab::frontend::parse(filename, "def broken(");
            throw std::runtime_error("invalid source accepted");
        } catch (const lab::DiagnosticError& error) {
            require(error.span.begin.file == filename, "error filename leaked");
        }
        auto tokens = lab::frontend::tokenize(filename, "\"a\\0b\"");
        require(tokens.size() == 2 && tokens[0].value == std::string("a\0b", 3), "lost string bytes");
        require(tokens.back().span.begin.offset == 6, "wrong EOF offset");
    }
}
} // namespace

int main() {
    try {
        std::vector<std::string> failures(4);
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&, i] {
                try { check_context(i); }
                catch (const std::exception& error) { failures[i] = error.what(); }
            });
        }
        for (auto& worker : workers) worker.join();
        for (const auto& failure : failures) require(failure.empty(), failure.c_str());

        lab::ast::Program program;
        program.files.push_back(lab::frontend::parse("empty.toy", ""));
        std::ostringstream output;
        lab::ast::dump_json(output, program);
        require(output.str().find("\"kind\": \"Program\"") != std::string::npos, "missing JSON root");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
