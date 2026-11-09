# 中端优化

实验分支 `experiment/middle-optimizations` 增加 `-O1`，默认等级为 `-O0`。语义检查完成后，优化器处理生成的 LLVM IR，再交给后端生成 ELF。

## 使用

```bash
mkdir -p out
build/labc --emit-llvm -O0 -o out/before.ll features/core.toy
build/labc --emit-llvm -O1 -o out/after.ll features/core.toy
build/labc --compile -O1 -o out/core features/core.toy
LAB_OPT_LEVEL=1 bash compile.sh features/core.toy out/core
bash run.sh out/core
```

`-O1` 需要 LLVM 的 `opt`，`setup.sh` 通过 llvm 包准备它。`LAB_OPT` 可以指定 opt 可执行路径，`LAB_CLANG` 指定 clang++ 路径，二者应选用兼容的 LLVM 版本。工具调用通过独立参数执行。优化失败返回 2，临时目录自动清理。

## 实现与顺序

`src/ir/optimize.cpp` 调用 LLVM New Pass Manager，并在每次运行后校验 IR。

| 阶段 | 实现 | 行为 |
| --- | --- | --- |
| 局部变量 SSA 提升 | LLVM mem2reg | 消除可提升的 alloca/load/store，构造 phi |
| 常量传播 | LLVM sccp | 沿可执行控制流边传播常量并求不动点 |
| 分支裁剪及不可达块删除 | LLVM simplifycfg | 折叠常量条件，清理失去可达路径的基本块 |
| 死代码消除 | LLVM adce | 删除对可观察行为没有贡献的指令 |
| 整数辅助调用折叠 | 项目自写 evaluate/fold | 求已知整数参数的安全运算，生成常量定义 |

源语言整数算术通过 `lab_int_op` 和 `lab_int_neg` 实现检查。优化器识别这些完整调用指令，仅当参数全为常量且计算成功时替换。之后重新运行 LLVM passes，让新常量继续传播。循环在没有可折叠调用时结束，能够处理多层常量表达式。

例如 `var x:int=2+3; if(x==5){return x*4;}else{return 1;}` 在优化后的函数体中可成为 `ret i64 20`，局部存储、算术调用和条件基本块都被删除。

## 语义约束

语义检查先于优化，因此不可达源分支中的类型错误仍在编译期报告。整数溢出、除零和最小整数取负保持运行时诊断。未使用的结果也可能产生错误，删除指令必须保留这些可观察行为。

用户函数、I/O、数组检查、分配和浮点运行时调用保留保守的副作用属性。整数辅助调用只有在证明计算成功后才能替换。浮点运行时算术目前保持原调用，LLVM 可优化其周围的存储和控制流。

## 测试

镜像入口仍为 `LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh`。CTest 增加 `native_compiler_o1` 和 `optimizer`，分别执行 O1 下的完整原生回归，以及结构和行为差分检查。

结构检查确认常量返回、无条件分支、无局部存储和无多余调用。行为检查逐字节比较 O0/O1 的退出码、stdout 和 stderr，覆盖运行时错误、I/O、循环、分支汇合、短路和并行结果。`-verify-each` 同时检查 LLVM IR 的合法性。
