# 一个程序的逐阶段导读

配套源码：[walkthrough.toy](../examples/tutorial/walkthrough.toy)。它已加入课程自声明功能回归，输出由同目录 `.out` 文件固定。

## 先读懂源程序

程序计算四个输入的平方乘以 0.5，再输出输入总和、最后一个映射结果和 5 的阶乘。

| 语句/结构 | 体现的规则 |
| --- | --- |
| `struct Result` | 具名结构体，构造实参按字段顺序 |
| `def factorial(...);` | 递归需要前置原型；声明不生成函数体 |
| `range(1, 5)` | 半开区间，输入为 `[1,2,3,4]` |
| `parallel map` | 捕获 factor，只读；local 是每次迭代的可变局部变量 |
| `local * factor` | int → float 数值提升，结果为 float |
| `for (... in input)` | 对数组做运行时循环 |
| `string(...)` | print 不接受隐式字符串转换 |

## 从字符到语法树

扫描器先识别关键字、名字、字面量和标点。例如 `0.5` 是一个 float literal，`-0.5` 是减号和字面量两个 token。每个 token 在扫描时更新源码范围。

Bison 按 `parser.y` 的文法归约，执行我们写的构造动作。`local * factor` 对应 Binary 节点，两个子节点为 Name。赋值和变量声明分别是语句节点；map 的末尾 yield 单独存入 result，不是普通 return。

运行 `--dump-ast --no-locations` 后可先找到 main 的 body，再找到 output 变量的 ParallelMap 初始化表达式。AST 只是结构，尚不证明 factor 存在或具有正确类型。

## 静态检查加上含义

检查器找到 local 对应的 int 绑定、factor 对应的 float 绑定，将乘法结果记为 float，并把 local 这个子表达式加入 promotions。并行捕获在检查期间只读；`factor = 2.0;` 会编译失败。

factorial 的调用形成自环，所以必须有先前的匹配原型。递归检查只分析调用关系，不运行函数。

## 看懂 LLVM IR

执行上手文档中的 `--emit-llvm`，从以下符号开始读：

| 符号/指令 | 在示例中的含义 |
| --- | --- |
| `@user_main`、`@user_factorial` | 源语言函数的生成定义 |
| `@map_0` 等 | 提取出的并行体，具体编号由生成顺序决定 |
| `@lab_int_to_float` | local 的整数数值转换为浮点位模式 |
| `@lab_float_op` | 浮点算术；操作码由 IR 生成器静态选择 |
| `@lab_parallel` | 提交输入、环境和回调，等待有序结果 |
| `alloca/load/store` | 局部变量槽及读写 |
| `br`、`ret` | 分支、循环与返回 |

IR 中大多看到 i64，是项目统一 ABI 的设计；浮点的 i64 是位模式，不能当整数直接相加。结构体字段也通过运行时对象槽访问，而不是复制 C++ 的结构体布局。

## 到原生程序

后端把 IR 写入临时文件，调用 clang++ 编译链接 `liblab_runtime.a`，成功后发布 ELF。运行时 main 调用生成的 `lab_entry`，进而调用 user_main；结束时处理状态码或错误。

可以尝试三处改动来检查理解：去掉递归原型、把 factor 改为 bool、在 map 内调用 print。三者应该分别被递归检查、类型检查和并行副作用检查拒绝；只导出 AST 则不会执行这些检查。
