# 工具链不能在非 ASCII 路径下链接

- 日期：2026-10-18
- 现象：`g++` 编译单个 .cpp 成功，链接时 ld 报一屏
  `cannot find G:/<乱码>/.toolchain/mingw64/.../crt2.o`、`cannot find -lstdc++`、`-lkernel32`。
- 代价：整条构建链卡死；误判成「MinGW 装坏了」会浪费数小时。
- 根因：MinGW 的 ld 用 ANSI 代码页（GBK）解释路径，而 UTF-8 的中文目录名解不出来。
  **编译阶段**（cc1plus）能过，所以症状只在链接出现。
- 试过且**无效**的方案：
  - 目录 junction（`mklink /J`）：内核把路径还原成真实的中文路径；
  - `subst` 盘符：同样被还原成真实路径；
  - 8.3 短路径：G: 盘未启用 8.3，短名等于长名。
- 有效方案：工具链放**物理 ASCII 路径**（本项目用 `C:\bp-toolchain`）。
- 判据：`C:\bp-toolchain\mingw64\bin\g++.exe hello.cpp -o hello.exe` 且 hello 能运行。
