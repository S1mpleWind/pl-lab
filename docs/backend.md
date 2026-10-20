# 中端、LLVM 后端与运行时

## 编译流程

```text
Flex/Bison → AST → sema::Checked → 文本 LLVM IR → Clang → Linux ELF
                                              + C++ 运行时静态库
```

`src/sema/check.cpp` 先收集所有文件的声明，再检查类型、词法作用域、明确初始化、返回路径及并行调用图。检查结果将表达式类型、变量编号、参数编号和并行捕获映射到 AST 节点；错误不会进入 IR 生成。

`src/ir/emit.cpp` 直接输出 LLVM IR，没有经 C/C++ 源码转译。函数、分支、循环、短路、调用和并行回调都是生成的 LLVM 基本块与指令。第一版使用 alloca/load/store 表示局部变量，不做 SSA 提升或自定义优化。

`src/backend/build.cpp` 通过 `fork/execvp` 传递工具参数，不使用 shell 拼接。Clang 接受 `.ll` 输入并完成机器码生成及与运行时库的链接；原子发布成功的 ELF，失败时清理临时目录。设置 `LAB_CLANG` 可选择兼容的 `clang++`。

## ABI

目标为 Linux x86-64。源语言 float 将 binary64 位模式编码为 `i64`，通过专用浮点运行时函数完成运算和检查；sema 显式记录 int → float 提升位置。源语言 int/bool 使用 `i64`（bool 只为 0/1）；字符串和复合值使用指针编码的 `i64` 句柄。此表示不允许源语言动态换类型，所有操作由 sema 静态限定。函数名使用 `user_` 前缀，与运行时 `lab_` 名称隔离。

运行时导出 `extern "C"` 函数。字符串保存显式长度；复合对象保存值槽及每槽是否为复合对象的标记，用于深复制。运行时不依赖 C++ 结构体布局传递源语言结构体。

并行回调 ABI 为 `i64 callback(i64 index, i64 element, i64 environment)`。环境含已初始化的捕获值，只在同步 map 调用期间借用。独立的运行时源文件实现线程池、队列、完成等待、最小出错下标记录与线程关闭，没有使用现成线程池或 OpenMP。

检查失败和运行时失败的错误语义见 [semantics.md](semantics.md)。C++ 异常仅在支持库内部传播，并经带展开表的生成调用栈到达运行时 main 的错误边界，不是源语言异常功能。

## 命令

```bash
bash build.sh
build/labc --check features/core.toy
build/labc --emit-llvm -o out/core.ll features/core.toy
bash compile.sh features/core.toy out/core
LAB_THREADS=4 bash run.sh out/core
```

默认 AST 和 `--dump-tokens` 保持前端工具接口，不执行语义检查。完整程序使用 `--check`、`--emit-llvm` 或 `--compile -o FILE`。退出码：成功 0，源程序错误 1，参数/文件/工具链错误 2；执行时由程序返回值或运行时错误决定状态。

`compile.sh SOURCE... OUTPUT` 会在编译前移除旧产物；先拒绝源文件的路径、符号链接及硬链接别名，避免删除源文件。脚本可从任意工作目录调用，源路径及产物路径相对调用者。
