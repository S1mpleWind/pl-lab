# CompilerLab

课程编译器项目，实验分支已接入 C++17 的 Flex/Bison 前端、静态语义分析、LLVM IR、原生 ELF 后端及有序并行运行时。语言设计见 [design.md](design.md)，阶段安排见 [plan.md](plan.md)，已落地文法与接口见 [docs/frontend.md](docs/frontend.md)。

## 本地开发

本地需要 C++17 编译器、CMake >= 3.16、Flex >= 2.6、Bison >= 3.8 、m4 和 Clang。Python 3 用于镜像中的回归测试。

```bash
# Ubuntu 上首次准备本地依赖，按需执行：
sudo apt-get install build-essential cmake flex bison m4 clang
bash build.sh

# 本地调试：查看 token 和 AST。
build/labc --dump-tokens examples/frontend/full.toy
build/labc --dump-ast examples/frontend/full.toy
mkdir -p out
build/labc --dump-ast --no-locations -o out/tree.json examples/frontend/full.toy
```

`labc` 接受多个源文件，保留输入顺序，输出一个 JSON 文档。默认输出 AST；所有文件解析成功后才写出结果。词法/语法错误返回 1，命令行、文件或内部错误返回 2，成功返回 0。诊断写入 stderr。

默认 AST 导出仅解析语法；完整编译使用：

```bash
build/labc --check features/core.toy
build/labc --emit-llvm -o out/core.ll features/core.toy
bash compile.sh features/core.toy out/core
LAB_THREADS=4 bash run.sh out/core
```

已实现规则见 [docs/semantics.md](docs/semantics.md)，编译链路与 ABI 见 [docs/backend.md](docs/backend.md)。首版不包含浮点类型。公开算法套件尚未提供，不能据此宣称完成课程最终验收。

## 镜像测试

开发和构建留在本地；自动回归测试使用课程镜像：

```bash
bash tests/in-image.sh
# 另跑课程提交接口、自声明功能及并行计时探针：
LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh
```

脚本首次使用 `materials/Dockerfile` 构建 `compilerlab-frontend:test`，传入干净源码，在镜像中安装前端依赖、重新构建并运行 CTest。不会挂载或执行本地编译产物。

需要运行中的 Linux Docker 引擎。WSL 用户须启动 Docker Desktop 并启用该发行版集成。可以用 `DOCKER` 指定客户端路径、用 `LAB_TEST_IMAGE` 指定兼容的课程测试镜像。未能启动 Docker 不算测试通过。

镜像内的三个测试入口为：

- `frontend_cli`：Python 标准库回归测试，检查 AST 结构、优先级、转义、诊断、多文件和命令行行为。
- `frontend_api`：C++ API 测试，检查重复解析、错误后重新解析及并发独立 scanner/parser 实例。

- `native_compiler`：语义反例、真实 ELF、运行时边界与不同线程数的有序并行回归。

## 代码组织

- `src/frontend/lexer.l`：正规表达式和词法状态。
- `src/frontend/parser.y`：Bison 文法，动作只构造 AST。
- `src/ast/`：AST、源码范围、JSON 导出。
- `src/sema/`：名字解析、类型、初始化、返回路径及并行安全检查。
- `src/ir/`、`src/backend/`：LLVM IR 与工具链驱动。
- `runtime/`：字符串、复合值和自建线程池。
- `src/driver/`：`labc` 命令行入口。
- `tests/`：前端测试与镜像入口。
- `materials/`：课程提供的模板和评测材料，独立保留。
