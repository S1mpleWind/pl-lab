# 中端优化

分支 `experiment/middle-optimizations` 提供 `-O1`。优化器在语义检查后处理本项目生成的 LLVM IR，默认等级为 `-O0`。实现和实验结果见 [实验报告](reports/middle-optimization-report.md)。

## 使用与导出

```bash
mkdir -p out
build/labc --emit-llvm -O0 -o out/before.ll examples/optimization/constant_branch.toy
build/labc --emit-llvm -O1 -o out/after.ll examples/optimization/constant_branch.toy
build/labc --emit-cfg -O0 -o out/before.dot examples/optimization/constant_branch.toy
build/labc --emit-cfg -O1 -o out/after.dot examples/optimization/constant_branch.toy
LAB_OPT_LEVEL=1 bash compile.sh examples/optimization/constant_branch.toy out/example
bash run.sh out/example
```

该示例正常退出码为 20。DOT 按函数分组，节点标签包含基本块名和主要指令，边带 true/false 标签。安装 Graphviz 后可执行 `dot -Tsvg out/after.dot -o out/after.svg` 查看。

## 自写分析与变换

实现入口为 `src/ir/optimize.cpp`。内部用 Module、Function、Block、Instruction 表示模块、函数、基本块和指令，显式记录执行结果、操作数和跳转目标。

| 阶段 | 算法及约束 |
| --- | --- |
| 跨块常量传播 | 正向数据流迭代，维护可执行边、SSA 值及不逃逸栈槽的常量事实，汇合时只保留各路径一致的事实 |
| 常量求值 | 求整数运算、比较、布尔运算及 phi 的常量值，溢出/除零保持原调用 |
| 分支裁剪 | 常量条件改为直接跳转 |
| 不可达删除 | 从入口重新计算可达集合，删除不可达块并修复 phi 输入 |
| 副作用分析 | 显式分类 I/O、堆写入、分配、运行时错误、未知行为及可能不终止，沿调用图求闭包 |
| 死代码消除 | 以可观察效果和控制流为根，反向标记 SSA 依赖及活跃栈槽，再删除未标记指令和无用局部写入 |
| 基本块合并 | 合并只有唯一前驱的跳转目标，更新后继 phi 的前驱标记 |

以上变换反复执行直到 IR 稳定。循环和递归被保守视为可能不终止，即使返回值未使用，也保留相应调用。未知指令和未知调用采用保守副作用分类。浮点运行时算术保留原调用。

## LLVM 使用范围

所有分析和变换由项目 C++ 代码实现。`opt -passes=verify -disable-output` 在优化前后检查 IR 合法性。`LAB_OPT` 指定 verifier 的路径，`LAB_CLANG` 指定后端 clang++，两者应使用兼容版本。工具错误返回 2，临时工作目录自动清理。

语义检查先于优化，因此不可达源分支中的类型错误仍会被拒绝。ABI 和从左到右求值约定见 [后端说明](backend.md)。

## 测试

`LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh` 从干净源码构建并运行全部回归。`native_compiler` 和 `native_compiler_o1` 覆盖两个等级，`optimizer` 专项检查结构、行为差分、跨块汇合、phi、副作用和非终止调用保留、CFG 结构，以及 LLVM 工具只执行 verify 的约束。
