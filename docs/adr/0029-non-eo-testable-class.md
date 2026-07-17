# ADR-0029：非 EO 类 UT Include 路由机制

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-14 | 韵启龙

---

## 背景

ADR-0028 规定了 EO 类的 UT 方案。非 EO 工具类需要编译期类替换机制。

---

## 决策

---

### 1. 三态定义

| 概念 | 含义 |
|------|------|
| `_orig` | UT 中直接用原始头文件（真实实现） |
| `_empty` | UT 中只用前向声明（走 `.fwd.hpp`） |
| `_mock` | UT 中用 mock 实现（gmock MOCK_METHOD） |
| 默认（不写） | 走 mock |

- **优先级**：`orig > empty > mock`
- **适用范围**：仅非 EO 类。EO 类（继承 `fw::EoBase`）不需要路由块。

---

### 2. 识别名

格式：`类名_后缀`，无路径，无命名空间。

| define | 示例 |
|--------|------|
| `Xxx_orig` | `SessionFlags_orig` |
| `Xxx_empty` | `SessionFlags_empty` |
| `Xxx_mock` | `SessionFlags_mock` |

约束：同一项目中所有类名必须唯一。

---

### 3. 宏工具（`src/common/testable/TestableMacros.hpp`）

```cpp
#pragma once

#if FLOWHUB_TEST_BUILD
#define TESTABLE_MOCK(PATH) PATH
#define USE_ORIG(CLASS)    defined(CLASS##_orig)
#define USE_EMPTY(CLASS)   defined(CLASS##_empty)
#else
#define TESTABLE_MOCK(PATH) "common/testable/TestableEmpty.hpp"
#define USE_ORIG(CLASS)    1
#define USE_EMPTY(CLASS)   0
#endif
```

- `USE_ORIG(Xxx)`：产品编译永远为 `1`（走真实代码），测试编译检查 `Xxx_orig` 是否 define
- `TESTABLE_MOCK(PATH)`：测试编译透传路径，产品编译指向一个空文件（永远走不到，仅保证语法合法）

每个有路由块的 `.hpp` 需在 include 区末尾引入此头文件。

---

### 4. `.hpp` 路由块写法

路由块位于 **所有 `#include` 之后、类定义之前**。分两种模式。

### 模式 A：有 `.fwd.hpp`（大部分非 EO 类）

```cpp
// Xxx.hpp
#pragma once

#include "Yyy.hpp"
// ... 所有 include ...
#include "common/testable/TestableMacros.hpp"

#if FLOWHUB_TEST_BUILD && !USE_ORIG(Xxx)
    #if USE_EMPTY(Xxx)
        #include "Xxx.fwd.hpp"
    #else
        #include TESTABLE_MOCK("MockXxx.hpp")
    #endif
#else

    class Xxx { /* ... 真实代码 ... */ };

#endif
```

| 写了什么 | 结果 |
|---------|------|
| `#define Xxx_orig` | `!USE_ORIG` 为假 → 真实代码 |
| `#define Xxx_empty` | `.fwd.hpp` |
| 什么都没写 | `MockXxx.hpp`（默认 mock） |

### 模式 B：无 `.fwd.hpp`（只需 mock 或 orig）

```cpp
// Xxx.hpp
#pragma once

#include "common/testable/TestableMacros.hpp"

#if FLOWHUB_TEST_BUILD && !USE_ORIG(Xxx)
    #include TESTABLE_MOCK("MockXxx.hpp")
#else

    class Xxx { /* ... 真实代码 ... */ };

#endif
```

| 写了什么 | 结果 |
|---------|------|
| `#define Xxx_orig` | 真实代码 |
| 什么都没写 | `MockXxx.hpp`（默认 mock） |

---

### 5. `.fwd.hpp` 写法

仅包含纯前向声明，不 include 任何东西：

```cpp
// Xxx.fwd.hpp
#pragma once

namespace xxx {
class Xxx;
} // namespace xxx
```

---

### 6. Mock 文件

Mock 是**独立类**，不继承真实类。因为 mock 在 include 层面完全替代了真实头文件，真实类此时并不可见，无需也不应继承。

> **例外**：多态类（如 SysLog）基类定义在路由块之前（始终编译），mock 继承该基类。

使用 gmock `MOCK_METHOD`，仅非 EO 类需要：

```cpp
// MockXxx.hpp
#pragma once

#include <gmock/gmock.h>

class MockXxx
{
  public:
    MOCK_METHOD(void, someMethod, (int arg));
    MOCK_METHOD(int, otherMethod, (const std::string &));
};
```

---

### 7. Test 文件用法

```cpp
// TestMyClass.cpp

#define DepA_mock       // 依赖 A 走 mock
#define DepB_orig       // 依赖 B 走真实
// DepC 没写 → 默认走 mock

#include "MyClass.hpp"
#include <gtest/gtest.h>

// ...
```

- `#define` 必须写在 `#include` 被测文件之前
- EO 类不需要 define，因为它们没有路由块，永远走 orig

---

### 8. 产品编译

产品代码不定义 `FLOWHUB_TEST_BUILD`，不定义任何 `_orig` / `_empty` / `_mock`。路由块的条件永远为假，走 `#else` → 真实实现。**零影响。**


### 9. 路由块与 namespace

`<gmock/gmock.h>` 在 namespace 内部 include 会导致 `testing::` 变为 `utils::testing::`。**路由块必须放在 namespace 外部**。

### 10. SysLog 示例

SysLog 多态（基类+指针），模式 B（无 empty）。UT 不写 `#define`，默认 mock。

---

## 设计取舍

| 取舍项 | 决策 | 理由 |
|--------|------|------|
| 默认 = mock | 不写 define 走 mock | 最常用场景 |
| 优先级 orig > empty > mock | 显式覆盖 > 默认 | 安全 + 灵活 |
| 路由块在 namespace 外 | 放在 namespace 闭合之后 | 避免 `testing::` 污染 |
| mock 不继承（一般）/ 继承基类（多态） | 按情况 | 多态需要基类指针 |
