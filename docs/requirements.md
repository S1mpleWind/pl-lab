# 课程能力与实现对应

依据根目录《2026编译原理课程实验要求.pdf》（2026-10-16 修订）。此表记录实现位置，不代替课程最终验收。

| 要求 | 实现 | 回归入口 |
| --- | --- | --- |
| 静态类型、明确转换（3–5 页），增加 float 扩展 | `src/sema/check.cpp`、`docs/semantics.md` | types_and_names、initialization_and_returns、float_static_errors |
| int64、ASCII 字符串、bool（6–8 页），附加 binary64 float | `runtime/runtime.cpp`、`src/ir/emit.cpp` | int64_arithmetic_and_bool、strings_and_input、runtime_boundaries |
| 从左到右与短路 | LLVM 分支、phi 与顺序发出调用 | short_circuit、order_and_value_semantics |
| 初始化及至少三层词法作用域 | 作用域栈、分支初始化状态合并 | scopes_and_control_flow、initialization_and_returns |
| 分支、运行时循环、函数及直接递归 | LLVM 基本块、函数与调用 | recursion_multifile_and_order、scopes_and_control_flow |
| 一维定长数组、自定义复合值 | 具名 struct、带长度对象、深复制 | defaults_empty_and_struct_return、struct_errors |
| 字符输入、输出、拼接、相等、子串 | runtime 的 `lab_*` 接口 | strings_and_input、non_ascii_input、runtime_boundaries |
| 正则 lexer、CFG 生成 parser（9–10 页） | `lexer.l`、`parser.y`、Flex/Bison | frontend_cli、frontend_api，Bison 冲突视为构建错误 |
| AST、语义分析、代码生成及定位 | 独立 AST/sema/ir/backend；SourceSpan | 所有三个 CTest 组 |
| 三选一：确定性并行（11 页 4.1 节） | 静态捕获限制、调用图、自建线程池、有序槽与等待 | parallel_captures_and_order、parallel_rejections、parallel_error_order |
| 可配置并行度及空输入 | `LAB_THREADS=1..1024`，默认 1 | empty_parallel、invalid_thread_config |
| 提交脚本与功能声明 | 四个 shell 脚本、`compilerlab.yaml`、课程 CI 配置 | `tests/submission-in-image.sh` |
| 必做算法及课程最终验收 | 公开算法套件未提供，尚未实现对应提交 | 待课程发布，不能标为已通过 |

运行原生回归：`LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh`。
该入口中的空算法 suite 仅用于自声明功能 smoke 测试，明确不是课程公开算法套件。

词法规则及正规表达式生成自动机的说明见 [frontend.md](frontend.md)；精确静态语义见 [semantics.md](semantics.md)；ABI、代码生成和并行运行时见 [backend.md](backend.md)。
