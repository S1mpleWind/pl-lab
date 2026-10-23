# 上手与调试

以下命令从仓库根目录执行。本地开发和构建，镜像中回归；不要把 `materials/student-template/` 当作本项目编译器。

## 1. 准备本地环境

目标为 Linux x86-64（包括 WSL）。需要 C++17、CMake ≥3.16、Flex ≥2.6、Bison ≥3.8、m4、Clang 和 Python 3。

```bash
sudo apt-get install build-essential cmake flex bison m4 clang python3
bash build.sh
```

`build.sh` 生成 `build/labc`、`build/liblab_runtime.a`；Flex/Bison 的生成文件在 `build/generated/frontend/`。这些不是手工维护的源码，也不提交 Git。`setup.sh` 在缺依赖且以 root 运行时安装依赖；普通用户缺依赖会得到安装提示。

## 2. 编译并运行导读示例

```bash
mkdir -p out
bash compile.sh examples/tutorial/walkthrough.toy out/tutorial
LAB_THREADS=4 bash run.sh out/tutorial
```

预期输出：`sum=10 scaled=8 factorial=120`，末尾换行。`compile.sh` 最后一个参数是输出文件，前面可列多个源文件。源文件共同构成一个程序，递归原型的先后顺序按文件参数顺序判断。

## 3. 观察每个阶段

```bash
build/labc --dump-tokens examples/tutorial/walkthrough.toy
build/labc --dump-ast --no-locations examples/tutorial/walkthrough.toy
build/labc --check examples/tutorial/walkthrough.toy
build/labc --emit-llvm -o out/tutorial.ll examples/tutorial/walkthrough.toy
```

默认输出 AST。token/AST 导出不检查类型；`--check` 成功时没有输出。`--emit-llvm` 必须先通过语义检查。可在 `.ll` 文件中搜索 `user_main`、`map_`、`lab_parallel`、`lab_int_to_float`。

`labc` 的返回码：0 成功，1 源程序错误，2 参数、文件或工具链错误。运行产物的退出状态由 main 返回值或运行时错误决定。诊断写 stderr，正常输出写 stdout。

## 4. 在镜像中验证

```bash
bash tests/in-image.sh
LAB_SUBMISSION_TESTS=1 bash tests/in-image.sh
```

前者干净构建并运行 CTest；后者还运行课程 grader 的自声明功能检查和计时探针。需要运行中的 Linux Docker 引擎。镜像缺失时脚本从 `materials/Dockerfile` 构建；额外依赖仍由 `setup.sh` 安装。

WSL 客户端不可用时，可指定 `DOCKER=/mnt/d/Program_Files/Docker/resources/bin/docker.exe`；实际路径按本机安装位置调整。也可用 `LAB_TEST_IMAGE` 指定已有课程兼容镜像。

`materials/` 当前是本地课程资料，未纳入项目 Git 提交。新克隆仓库需要自行放入课程提供的 materials，或使用已有兼容镜像。完整提交接口回归还要求镜像内有 `/opt/compilerlab/grader`。

## 常见问题

| 现象 | 检查位置 |
| --- | --- |
| 找不到 Bison/Flex/CMake | 本地依赖和 PATH；不要复制其他机器的 build 目录 |
| 能导出 AST，但无法编译 | `--check` 诊断；语法合法不等于语义合法 |
| 递归函数报错 | 是否在定义前写了匹配的 `def f(...)->T;` |
| float 参数拒绝整数 | 参数和返回值要求精确类型，显式写 `float(value)` |
| runtime library missing | 重新运行 `bash build.sh`，保留 labc 同目录的静态库 |
| LLVM linking failed | Clang、完整 stderr 和 `LAB_CLANG` 是否指向兼容 clang++ |
| Docker 构建出现 502 | 软件源/网络问题，不能记为测试通过 |
