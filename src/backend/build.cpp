#include "backend/build.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace lab::backend {
namespace {
struct Temporary {
    std::filesystem::path directory;
    explicit Temporary(const std::filesystem::path &parent) {
        auto pattern = (parent / ".labc-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back(0);
        char *path = mkdtemp(buffer.data());
        if (!path)
            throw std::runtime_error("cannot create compiler workspace: " +
                                     std::string(std::strerror(errno)));
        directory = path;
    }
    ~Temporary() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
void run(const std::vector<std::string> &args) {
    auto pid = fork();
    if (pid < 0)
        throw std::runtime_error("cannot start LLVM toolchain");
    if (pid == 0) {
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::perror(argv[0]);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            throw std::runtime_error("cannot wait for LLVM toolchain");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("LLVM code generation or linking failed");
}
} // namespace
void build(const std::string &llvm, const std::filesystem::path &output,
           const std::filesystem::path &runtime) {
    if (!std::filesystem::is_regular_file(runtime))
        throw std::runtime_error("runtime library missing; run build.sh");
    auto destination = std::filesystem::absolute(output);
    std::filesystem::create_directories(destination.parent_path());
    Temporary workspace(destination.parent_path());
    auto source = workspace.directory / "program.ll", artifact = workspace.directory / "program";
    {
        std::ofstream stream(source);
        stream << llvm;
        stream.close();
        if (!stream)
            throw std::runtime_error("cannot write LLVM IR");
    }
    const char *configured = std::getenv("LAB_CLANG");
    std::string clang = configured ? configured : "clang++";
    run({clang, "-O0", "-Wno-override-module", "-x", "ir", source.string(), "-x", "none",
         runtime.string(), "-pthread", "-o", artifact.string()});
    std::filesystem::rename(artifact, destination);
}
} // namespace lab::backend
