# ADR-0007：消息、类型、Context 统一代码生成

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-05-26 | 韵启龙 |

---

## 背景

项目涉及大量消息类型、类型别名、Context 结构体。手动编写 C++ 代码存在以下问题：

1. **不一致**：消息定义和 CAF 序列化代码分属不同文件，字段变更容易遗漏同步
2. **重复**：每种消息需要手写 struct、CAF inspect 函数、消息注册——大量样板代码
3. **Context 对齐**：Context 需要 `alignas(64)` 和 cache line padding，手写容易出错
4. **日志负担**：消息和 Context 需要 `to_string()` 用于打 log。每个数据结构手写 `to_string` 重复劳动重，新增字段后容易遗漏更新

需要一个统一的定义源，自动生成所有 C++ 代码。

---

## 决策

### 1. `.mt` 定义文件作为唯一数据源

```
src/common/type/*.mt           → 类型别名
src/common/message/*.mt        → 消息/结构体
src/common/context/*.mt        → Context 结构体
```

`.mt` 语法：

```mt
# 类型别名（type/ 目录）
define UserId = uint8

# 消息（message/ 目录）
include message/UserHead.mt    ← 跨文件引用

message AiChatBusinessReq
    UserHead head
    string content

# Context（context/ 目录）
context AiChatContext
    uint8[64] modelName
    double temperature
    cacheLinePadding           ← 强制跨 cache line 隔离
```

### 2. Python 脚本自动生成

`scripts/gen_code.py` 解析所有 `.mt` 文件，生成：

| 产物 | 位置 |
|------|------|
| 类型别名 | `src/generated/Types.hpp` |
| 消息结构体 + `to_string()` | `src/generated/message/<Name>.hpp` |
| CAF 消息注册 | `src/generated/message/Messages.hpp` |
| Context 结构体 + `to_string()` | `src/generated/context/<Name>Context.hpp` |
| TaskType 枚举 | `src/generated/TaskType.hpp` |

- Context 根据 `.mt` 中的 `cacheLinePadding` 标记插入 `alignas(64)` 边界，自动附带 `to_string()`

### 3. 构建集成

CMake 自动收集所有 `.mt` 文件，`gen_code` 作为构建前置步骤。新增 `.mt` 无需手动注册。

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 手写所有代码 | 字段变更需同步多处，易遗漏 |
| 从 C++ struct 反向推导序列化 | C++ 类型信息在编译期不可用于生成 CAF inspect |
| Protobuf / FlatBuffers | 引入外部依赖；消息间 include 关系复杂；CAF 序列化需额外适配层 |

---

## 影响

- 新增消息类型只需写 `.mt` → 构建时自动生成 C++ 代码
- 字段变更只改一处，编译期即可发现不一致
- `src/generated/` 目录由脚本产出，不手动编辑
