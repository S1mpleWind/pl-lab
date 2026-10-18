#include "ast/json.hpp"
#include "frontend/parse.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void usage(std::ostream& out) {
    out << "usage: labc [--dump-ast | --dump-tokens] [--no-locations] [-o FILE] SOURCE...\n"
           "Frontend only: parse source files and emit JSON; no type checking or code generation.\n";
}

void position(std::ostream& out, const lab::SourcePosition& p) {
    out << "{\"file\":";
    lab::ast::write_json_string(out, p.file);
    out << ",\"line\":" << p.line << ",\"column\":" << p.column << ",\"offset\":" << p.offset << '}';
}

void tokens_json(std::ostream& out, const std::vector<std::string>& paths, bool locations) {
    out << "{\"files\":[";
    bool first_file = true;
    for (const auto& path : paths) {
        auto tokens = lab::frontend::tokenize(path, lab::frontend::read_source(path));
        if (!first_file) out << ',';
        first_file = false;
        out << "{\"path\":";
        lab::ast::write_json_string(out, path);
        out << ",\"tokens\":[";
        bool first_token = true;
        for (const auto& token : tokens) {
            if (!first_token) out << ',';
            first_token = false;
            out << "{\"kind\":";
            lab::ast::write_json_string(out, token.kind);
            if (token.has_value) {
                out << ",\"value\":";
                lab::ast::write_json_string(out, token.value);
            }
            if (locations) {
                out << ",\"span\":{\"begin\":";
                position(out, token.span.begin);
                out << ",\"end\":";
                position(out, token.span.end);
                out << '}';
            }
            out << '}';
        }
        out << "]}";
    }
    out << "]}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool token_mode = false;
        bool ast_mode = false;
        bool locations = true;
        bool options = true;
        std::string output;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (options && arg == "--") options = false;
            else if (options && (arg == "--help" || arg == "-h")) { usage(std::cout); return 0; }
            else if (options && arg == "--dump-tokens") token_mode = true;
            else if (options && arg == "--dump-ast") ast_mode = true;
            else if (options && arg == "--no-locations") locations = false;
            else if (options && (arg == "-o" || arg == "--output")) {
                if (++i == argc) throw std::runtime_error("missing output path");
                output = argv[i];
                if (output.empty()) throw std::runtime_error("output path must not be empty");
            } else if (options && !arg.empty() && arg.front() == '-') {
                throw std::runtime_error("unknown option: " + arg);
            } else paths.push_back(arg);
        }
        if (paths.empty()) { usage(std::cerr); return 2; }
        if (ast_mode && token_mode) throw std::runtime_error("choose one dump mode");
        if (!output.empty()) {
            const auto destination = std::filesystem::weakly_canonical(output);
            for (const auto& path : paths) {
                std::error_code error;
                const bool same_file = std::filesystem::equivalent(output, path, error);
                if ((!error && same_file) || destination == std::filesystem::weakly_canonical(path))
                    throw std::runtime_error("output must not overwrite a source file");
            }
        }

        // Buffer the complete result so a later-file error cannot leak partial JSON.
        std::ostringstream rendered;
        if (token_mode) tokens_json(rendered, paths, locations);
        else lab::ast::dump_json(rendered, lab::frontend::parse_files(paths), locations);
        if (output.empty()) {
            std::cout << rendered.str();
            if (!std::cout) throw std::runtime_error("cannot write stdout");
        } else {
            std::ofstream stream(output, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open output file: " + output);
            stream << rendered.str();
            stream.close();
            if (!stream) throw std::runtime_error("cannot write output file: " + output);
        }
        return 0;
    } catch (const lab::DiagnosticError& error) {
        std::cerr << error.span << ": " << error.category << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "labc: " << error.what() << '\n';
        return 2;
    }
}
