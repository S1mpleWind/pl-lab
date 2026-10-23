# 开发、测试与排错

## 修改功能时沿着同一条链路检查

以增加一种表达式为例：

1. `lexer.l` 增加 token，`parser.y` 增加产生式和 AST 构造动作；不要修改 build 中的生成文件。
2. `ast.hpp` 增加节点及 Kind，`ast.cpp` 和 `json.cpp` 增加名称与序列化。
3. `sema/check.cpp` 定义类型、合法操作、初始化和并行约束。先拒绝错误程序，再进入 IR。
4. `ir/emit.cpp` 保持从左到右求值，必要时增加运行时 ABI；不要用 C++ 求值顺序代替源语言约定。
5. 若增加 ABI，同步运行时实现和 LLVM declare；避免签名/参数数目不一致。
6. 更新规范、正例、反例和边界场景；本地构建后，在镜像中运行受影响回归。

只改变文档或注释不应宣称新增语言能力。构建失败先看最早的诊断；Bison 冲突已经配置成构建错误，不要通过忽略警告掩盖文法歧义。

## 测试分层

| 入口 | 验证范围 |
| --- | --- |
| `tests/frontend_test.py` | token/AST JSON、优先级、位置、语法/词法错误、多文件及 CLI |
| `tests/frontend_api.cpp` | scanner/parser 实例隔离、错误恢复后重新解析、并发调用 |
| `tests/compiler_test.py` | 真正编译 ELF 并运行；类型反例、数值/数组边界、复制、递归和并行 |
| `compilerlab.yaml` | 课程 grader 可以调用的功能程序、输入、输出和编译错误声明 |
| `tests/parallel_benchmark.py` | CPU 计时探针及线程数之间的结果一致性，不保证固定加速比 |

`LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh` 会从源码在独立容器构建、运行 CTest、validate、grader 和计时探针。不复用宿主机二进制。自声明 grader 使用明确标识的空算法 suite，只验证功能接口。

公开算法 suite 尚未提供。发布后应增加实际算法源程序及 manifest 条目，使用课程 suite 运行；不得用模板中的示例算法或当前功能测试替代。

## 定位一条失败用例

1. 用 `--dump-tokens` 确认扫描类别和跨度。
2. 用 `--dump-ast` 确认树结构；若结构正确再看 `--check`。
3. 如果 --check 失败，判断是名字、类型、初始化、原型还是副作用错误。
4. 若 --check 成功但行为错误，导出 `.ll`，沿 user_* 函数检查求值顺序、分支和运行时调用。
5. 只有并行失败时，比较 `LAB_THREADS=1/2/4`；关注对象生命周期、捕获写入、完成等待和错误下标，而不只看输出顺序。

`compile.sh` 会清理旧产物；失败后不要执行旧文件。运行时错误为 stderr 诊断及非零状态，目前不附源语言调用栈。`LAB_CLANG` 指定 clang++ 的可执行路径，不是带参数的 shell 命令。

## 注释和 Git

关键代码注释解释约束、生命周期、锁和数据流，而不是逐行复述 C++。新增规则同时更新 [semantics.md](semantics.md)、[design.md](../design.md) 和要求对应表。

按功能提交实现与对应测试；不要提交 build、out、reports、临时依赖或镜像。可以使用 `.clang-format` 规定的 4 空格格式。课程材料与个人未提交文件应单独处理，不使用 `git add .` 混入当前变更。
