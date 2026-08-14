# 项目 3：扩展 Flex 使其支持 Unicode 编码

本项目对经典词法分析器生成工具 **flex**（版本 2.6.4）做了扩展，使其能够
在词法规则（`pattern`）和定义（`definitions`）中直接书写 `\uXXXX`、
`\u{XXXXXX}` 以及 Unicode 字符类区间 `[\uXXXX-\uYYYY]`，例如：

```lex
%%
UNICODE_CHINESE   [\u4e00-\u9fff]
UNICODE_GREEK     [\u0370-\u03FF]

{UNICODE_CHINESE}+  { printf("Chinese: %s\n", yytext); }
{UNICODE_GREEK}+    { printf("Greek:   %s\n", yytext); }
%%
```

标准 flex 是**面向字节**的：它的字符类只是 0–255 字节的集合，正则表达式在
字节上匹配，因此它并不认识 `\u` 转义。本扩展在 flex 内部增加了一个
“Unicode 展开”阶段，把 `\u` 转义在送入 flex 自身的扫描器之前，改写成等价
的**字节级** flex 模式（UTF-8 编码），从而在不改动 flex 的 NFA/DFA 引擎的
前提下获得 Unicode 支持。

## 实现原理

一个 Unicode 码点 `U+4E2D`（“中”）的 UTF-8 编码是三个字节 `E4 B8 AD`，所以

```
\u4e2d            ==>   (\xE4\xB8\xAD)
```

一个码点区间 `[\u4e00-\u9fff]`（CJK 统一表意文字）则被展开为所有落入该区间
码点的 UTF-8 编码的并集（用标准的“三路切分”算法得到紧凑的字节区间正则）：

```
[\u4e00-\u9fff]   ==>   ( \xE4[\xB8-\xBF][\x80-\xBF]
                        | [\xE5-\xE8][\x80-\xBF][\x80-\xBF]
                        | \xE9[\x80-\xBF][\x80-\xBF] )
```

这样 `{UNICODE_CHINESE}+` 就等价于“一个或多个三字节 UTF-8 汉字”，`+` 作用在
整个 `(...)` 分组上，语义正确。

## 修改的文件

| 文件 | 改动 |
|------|------|
| `flex-2.6.4/src/unicode.c` | **新增**：Unicode 展开的实现（UTF-8 编码、区间切分、`[...]` 类展开、`.l` 文件分段/上下文状态机）。 |
| `flex-2.6.4/src/unicode.h` | **新增**：`unicode_expand()` 接口声明。 |
| `flex-2.6.4/src/scan.l` | **修改**：`set_input_file()` 现在先把输入文件读入内存，经 `unicode_expand()` 展开后通过临时文件交给 flex 扫描器。 |
| `flex-2.6.4/src/Makefile.am` | **修改**：把 `unicode.c`/`unicode.h` 加入 `COMMON_SOURCES`。 |

> 注：`scan.c` 是由 `scan.l` 经 flex 重新生成的（`flex -o scan.c scan.l`）。
> 重新构建时如果修改了 `scan.l`，需要先重新生成 `scan.c`（见下文）。

## 展开规则与范围

- 只改写**规则段（section 2）的 pattern** 和**定义段（section 1）的定义值**；
- section 3 用户代码、`%{ ... %}` 代码块、注释、action（动作）中的 C 代码
  一律原样拷贝，不会被误改；
- `\uXXXX`（恰 4 位十六进制）和 `\u{XXXXXX}`（1–6 位十六进制）均支持；
- 字符类中的 `\uXXXX-\uYYYY` 区间、混合类（如 `[a-z\u4e2d]`）均支持；
- 跨 UTF-8 字节长度的区间（如 `[\u00e9-\u4e2d]`，2 字节→3 字节）也支持。

### 已知限制

- 对“取反的 Unicode 类” `[^ \uXXXX...]` 暂不支持（会打印警告并原样保留）；
  字节级取反在 UTF-8 上语义复杂，属合理裁剪。
- 结果为字节级匹配：`yyleng` 是**字节数**而不是字符数（示例中 `Chinese text
  (12 bytes)` 即 4 个汉字 × 3 字节）。
- 需要输入为合法的 UTF-8 编码。

## 构建

需要：`gcc`、`make`、`autoconf`/`automake`（重新生成 `Makefile.in` 时才需要）。

```bash
cd project3-flex-unicode
make            # 等价于: cd flex-2.6.4 && ./configure && make
make test       # 生成/编译/运行 Unicode 测试
```

若修改了 `scan.l` 并希望重新生成 `scan.c`：

```bash
cd flex-2.6.4/src
flex -o scan.c scan.l          # 用任意已安装的 flex 2.6.4 引导
cd ../..
make
```

## 测试

`make test` 会运行两个词法文件：

- `tests/example-unicode.l`：中文/希腊文/箭头/符号/ASCII 的分类匹配；
- `tests/example-single.l`：单个 `\u4e2d`、`\u{1F600}`（emoji）、混合类
  `[a-zA-Z\u4e2d\u6587]`、跨字节长度区间 `[\u00e9-\u4e2d]`。

参考输出：

```
Chinese text (12 bytes): 你好世界
Greek: αβγδ
Arrow: →
Symbol: ★
Symbol: ☀
ASCII:  hello
Chinese text (6 bytes): 中文
```
