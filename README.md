# asf_enumvar

Rust-`enum`-like variants for C++26, driven by static reflection (P2996).
Single header: [`include/enumvar.hpp`](include/enumvar.hpp) —
named after the namespace `asf::enumvar`. Boost Software License 1.0.

[English](#english) · [中文](#中文)

---

## English

`asf_enumvar` turns a plain C++ enum into a tagged union,
`asf_enumvar::var<Enum>`, in which every enumerator carries a typed
payload — just like a Rust `enum`. Unlike `std::variant`, the type is
keyed by *enumerators*: `index()` returns the enumerator itself, and
construction, access and matching all spell the alternative as `E::x`.

```cpp
#include <enumvar.hpp>

struct Circle { double r; double area() const { return 3.14159 * r * r; } };
struct Rect   { double w, h; };

enum class Shape {
  circle [[=asf_enumvar::alt<Circle>]],
  rect   [[=asf_enumvar::alt<Rect>]],
  empty,                                // no annotation -> std::monostate
};
using ShapeVar = asf_enumvar::var<Shape>;

int main() {
  ShapeVar v(asf_enumvar::tag<Shape::circle>, Circle{2.0});

  double d = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::circle>([](const Circle& c) { return c.area(); }),
      [](auto&) { return -1.0; });      // final fallback for the rest
}
```

### Features

- **Enum-keyed interface.** `index()` returns the enumerator (type
  `Enum`, not `size_t`); `tag<E::x>` constructs, `get<E::x>` /
  `get_if<E::x>` access, `on<E::x>` arms a `match`.
- **Two ways to spell the alternatives.** Per-enumerator annotations
  (`[[=asf_enumvar::alt<T>]]`), or an explicit tuple:
  `var<E, std::tuple<A, B>>`. A tuple shorter than the enum (including
  `std::tuple<>`) leaves the trailing enumerators fieldless.
- **Fieldless enums are free.** When every alternative is
  `std::monostate`, `var<E>` *is* the enum — same size, same
  triviality, no storage overhead.
- **`std::variant`-grade semantics.** `valueless_by_exception()` +
  `std::bad_variant_access`, `emplace`, `swap`, `==` / `<=>`,
  copy/move/destructor triviality propagated from the alternatives, and
  `std::variant_size` / `std::variant_alternative` interop.
- **Embedded discriminant.** The payload sits at offset 0 of an inline
  byte storage; the tag is a real `Enum` member of the same object.
- **`match` with exhaustiveness checking.** Arms keyed by one or more
  enumerators; an optional final fallback (detected by a type check on
  the last argument) covers the rest.

### Requirements

- A C++26 compiler with static reflection (P2996). Currently that means
  **GCC 16+** with `-freflection`, linked against the experimental
  standard library (`-lstdc++exp`, for `std::meta`).
- `var` is a **runtime** facility: construction and destruction dispatch
  through the runtime tag, so it is not usable in constant expressions.

### Alternatives: annotations or tuple

```cpp
// annotation mode (default): the payload type rides on the enumerator
enum class Task {
  idle,
  run  [[=asf_enumvar::alt<std::string>]],
  stop,
};

// tuple mode: alternatives in declaration order
enum class Code : int { ok = 0, not_found = 404, server_err = 500 };
using CodeVar = asf_enumvar::var<Code, std::tuple<int, std::string, Blob>>;

// short tuple: trailing enumerators are pure values
using FlavorVar = asf_enumvar::var<Flavor, std::tuple<std::string>>;
```

`std::monostate` is the single spelling of "fieldless": in the tuple, as
an explicit `[[=asf_enumvar::alt<std::monostate>]]`, or implicitly.

### `match`

```cpp
asf_enumvar::match(v,
  asf_enumvar::on<Color::red>              ([](const Rgb& c) { ... }),
  asf_enumvar::on<Color::green, Color::blue>([](const auto& x) { ... }),
  [](auto&) { ... });   // optional FINAL fallback
```

- An `on<Vs...>` arm covers **one or more** enumerators with a single
  callable.
- The last argument is the fallback iff it is **not** an `on<>` arm — a
  pure type check. With no fallback, the arms must cover every
  enumerator; with one, the unarmed rest dispatches to it.
- An arm and the fallback alike are invoked with the active
  alternative, or nullary (`[] { ... }`) when the payload is not needed
  — the natural spelling for `std::monostate` alternatives, but valid
  for any. A fallback taking an argument must accept EVERY uncovered
  alternative. All of them must yield the same return type.
- Compile-time checked: every `on<V>` must name an enumerator of the
  matched enum, no enumerator may be armed twice, and non-exhaustive
  matches without a fallback are rejected with a `static_assert`.
- Runtime-checked like `visit`: throws `std::bad_variant_access` on a
  valueless `var`.

A single plain callable (e.g. one generic lambda) is the all-fallback
case and dispatches like `visit`:

```cpp
asf_enumvar::match(v, [](auto& alt) { ... });
```

### API overview

Everything lives in `namespace asf::enumvar`, aliased as `asf_enumvar`.

| Entity | Meaning |
| --- | --- |
| `alt<T>`, `alt_tag` | `[[=asf_enumvar::alt<T>]]` enumerator annotation |
| `var<Enum, Tuple = void>` | the tagged union; `Tuple = void` → annotation mode |
| `tag<E::x>` | tag for by-enumerator construction: `var{tag<E::x>, args...}` |
| `v.index()` | the **enumerator** of the active alternative |
| `v.valueless_by_exception()` | variant-style valueless state |
| `v.emplace<E::x>(args...)` / `v.emplace<T>(args...)` | destroy + reconstruct in place |
| `v.swap(o)`, `swap(a, b)` | member and free swap |
| `get<E::x>(v)` / `get<T>(v)` | access, throws `std::bad_variant_access` |
| `get_if<E::x>(ptr)` / `get_if<T>(ptr)` | access, `nullptr` on mismatch |
| `holds_alternative<E::x>(v)` / `holds_alternative<T>(v)` | active-alternative query |
| `visit(f, v)` / `visit(f, v1, v2, ...)` | single- and multi-var visitation |
| `match(v, arms...[, fallback])`, `on<Vs...>(f)` | enum-keyed matching |
| `std::variant_size`, `std::variant_alternative` | std interop specializations |

### Building and testing

Header-only — add `include/` to your include path,
`#include <enumvar.hpp>`, and compile with GCC 16+:

```sh
g++ -std=c++26 -freflection your.cpp -lstdc++exp
```

Or consume it through CMake:

```cmake
add_subdirectory(asf_enumvar)
target_link_libraries(your_target PRIVATE asf_enumvar::asf_enumvar)
```

Run the tests:

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

### Examples

[`examples/`](examples) contains small narrative demos meant for
reading, not for verification — that is what distinguishes them from
`tests/` (which hold the assertion batteries and edge cases and run
under CTest):

- [`examples/shapes.cpp`](examples/shapes.cpp) — annotation mode:
  enum-keyed construction/access, a `match` with a multi-enumerator
  arm and a final fallback.
- [`examples/connection.cpp`](examples/connection.cpp) — tuple mode: a
  connection state machine with fieldless and payload states.

They build by default in a top-level build
(`-DASF_ENUMVAR_BUILD_EXAMPLES=ON/OFF`) but are deliberately **not**
registered with CTest; CI smoke-runs them instead.

### Continuous integration

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds and tests
on Ubuntu (GCC 16 from `ubuntu-toolchain-r/test`) and Windows (MSYS2
UCRT64 GCC), then smoke-runs the examples.

---

## 中文

`asf_enumvar` 借助 C++26 静态反射（P2996)，把一个普通 C++ 枚举变成带标签联合
`asf_enumvar::var<Enum>`：每个枚举项都可以携带一个有类型的载荷——就像
Rust 的 `enum`。与 `std::variant` 不同，它以**枚举项**为键：`index()`
返回枚举值本身，构造、访问与匹配都用 `E::x` 书写。

```cpp
#include <enumvar.hpp>

struct Circle { double r; double area() const { return 3.14159 * r * r; } };
struct Rect   { double w, h; };

enum class Shape {
  circle [[=asf_enumvar::alt<Circle>]],
  rect   [[=asf_enumvar::alt<Rect>]],
  empty,                                // 无注解 -> std::monostate
};
using ShapeVar = asf_enumvar::var<Shape>;

int main() {
  ShapeVar v(asf_enumvar::tag<Shape::circle>, Circle{2.0});

  double d = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::circle>([](const Circle& c) { return c.area(); }),
      [](auto&) { return -1.0; });      // 末位回落，覆盖其余枚举项
}
```

### 特性

- **枚举键接口。** `index()` 返回枚举项（类型是 `Enum` 而非
  `size_t`)；`tag<E::x>` 构造、`get<E::x>` / `get_if<E::x>` 访问、
  `on<E::x>` 做 `match` 分支。
- **两种备选类型写法。** 逐项注解（`[[=asf_enumvar::alt<T>]]`)，或显式
  tuple:`var<E, std::tuple<A, B>>`。tuple 短于枚举时（含
  `std::tuple<>`)，尾部枚举项无载荷。
- **无载荷枚举零开销。** 全部备选都是 `std::monostate` 时，`var<E>`
  就是枚举本身——同尺寸、同平凡性，无存储开销。
- **`std::variant` 级语义。** `valueless_by_exception()` +
  `std::bad_variant_access`、`emplace`、`swap`、`==` / `<=>`，拷贝/移动/
  析构平凡性由备选类型传播，并提供 `std::variant_size` /
  `std::variant_alternative` 互操作。
- **内嵌判别字。** 载荷位于内联字节存储偏移 0 处；标签是同一对象中
  真实的 `Enum` 成员。
- **带穷举检查的 `match`。** 分支可绑定一个或多个枚举项；末位可选回落
  （通过类型检查判定）覆盖其余项。

### 环境要求

- 支持静态反射（P2996）的 C++26 编译器。目前即 **GCC 16+**，加
  `-freflection`，并链接实验性标准库（`-lstdc++exp`，提供
  `std::meta`)。
- `var` 是**运行时**设施：构造与析构经由运行时标签派发，不能用于常量
  表达式。

### 备选类型：注解或 tuple

```cpp
// 注解模式（默认）：载荷类型随枚举项声明
enum class Task {
  idle,
  run  [[=asf_enumvar::alt<std::string>]],
  stop,
};

// tuple 模式：按声明顺序给出备选
enum class Code : int { ok = 0, not_found = 404, server_err = 500 };
using CodeVar = asf_enumvar::var<Code, std::tuple<int, std::string, Blob>>;

// tuple 更短：尾部枚举项是纯值
using FlavorVar = asf_enumvar::var<Flavor, std::tuple<std::string>>;
```

`std::monostate` 是"无载荷"的唯一写法：出现在 tuple 中、显式
`[[=asf_enumvar::alt<std::monostate>]]` 注解，或隐式缺省。

### `match`

```cpp
asf_enumvar::match(v,
  asf_enumvar::on<Color::red>              ([](const Rgb& c) { ... }),
  asf_enumvar::on<Color::green, Color::blue>([](const auto& x) { ... }),
  [](auto&) { ... });   // 可选的末位回落
```

- 一个 `on<Vs...>` 分支用单个可调用体覆盖**一个或多个**枚举项。
- 末位实参**不是** `on<>` 分支时即为回落——纯类型检查。没有回落时分支
  必须覆盖全部枚举项；有回落时未覆盖项派发到回落。
- 分支与回落一样以当前备选调用；不需要载荷时可写空参形式
  （`[] { ... }`)——这是 `std::monostate` 备选的自然写法，但对任何备选
  都有效。带参回落必须能接受**每个**未覆盖备选。所有分支与回落必须
  返回同一类型。
- 编译期检查：每个 `on<V>` 必须是被匹配枚举的枚举项、同一枚举项不可
  重复绑定、无回落且不穷举会被 `static_assert` 拒绝。
- 与 `visit` 一样做运行时检查：`var` 为 valueless 时抛
  `std::bad_variant_access`。

单个普通可调用体（如一个泛型 lambda）即"全回落"情形，派发行为同
`visit`:

```cpp
asf_enumvar::match(v, [](auto& alt) { ... });
```

### 构建与测试

单头文件——把 `include/` 加进包含路径，`#include <enumvar.hpp>`,
用 GCC 16+ 编译：

```sh
g++ -std=c++26 -freflection your.cpp -lstdc++exp
```

或经 CMake 引入：

```cmake
add_subdirectory(asf_enumvar)
target_link_libraries(your_target PRIVATE asf_enumvar::asf_enumvar)
```

运行测试：

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

### 示例

[`examples/`](examples) 收录的是供阅读的叙事式小演示，而非正确性验证
——这正是它与 `tests/` 的区别（测试承载断言与边界情形，由 CTest 驱动）:

- [`examples/shapes.cpp`](examples/shapes.cpp)——注解模式：枚举键
  构造/访问，带多枚举项臂与末位回落的 `match`。
- [`examples/connection.cpp`](examples/connection.cpp)——tuple 模式：
  含无载荷与有载荷状态的连接状态机。

顶层构建默认编译示例（`-DASF_ENUMVAR_BUILD_EXAMPLES=ON/OFF`)，但刻意
**不**注册进 CTest;CI 里仅做冒烟运行。

### 持续集成

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) 在
Ubuntu(`ubuntu-toolchain-r/test` 的 GCC 16）与 Windows(MSYS2
UCRT64 GCC）上构建并测试，随后冒烟运行示例。

### License

[Boost Software License 1.0](LICENSE) (BSL-1.0)
