# ADR-0010：Business 层 EO 通过 TaskType 模板参数绑定 Context

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳（修订） | 初版 2026-06-12，修订 2026-06-14 | 韵启龙 |

---

## 背景

ADR-0009 确定了 Context 存储于 `TaskPool` 中，EO 通过 GTID 访问 Context。但仅靠 GTID 无法在编译期建立 EO 与 Context 类型的绑定关系。

当前 `EoBase` 只接受一个模板参数 `Derived`（CRTP），不约束 EO 能操作哪种 Context。这意味着：

1. 任何 EO 理论上可以传入任意 GTID 请求任意类型的 Context，错误只能在运行时暴露
2. EO 代码中无法从类型系统获知"我的 Context 是什么"，IDE 补全和重构均无感知
3. 新增 Context 类型时，编译器不会提示哪些 EO 需要适配

---

## 决策（初版，2026-06-12）

**所有需要读写 Context 的 Business 层 D 面 EO，必须通过自身模板参数声明其绑定的 Context 类型。`EoBase` 不感知此参数。**

```cpp
// EO 自身声明 ContextType 模板参数，EoBase 不变
template <typename ContextType>
class AiChatBus : public fw::EoBase<AiChatBus<ContextType>>
{
    // 此 EO 只能读写 ContextType 类型的 Context
};
```

**接口一致性**：`TaskPool::getContext` 的模板参数与 EO 的 `ContextType` 统一，调用时直接传递：

```cpp
// EO 内部取 Context
pool.getContext<ContextType>(gtid).useOrFailed(
    [&](ContextType& ctx) { /* 类型安全，IDE 可补全 */ },
    []()                   { /* 类型不匹配或 slot 无效 */ }
);
```

**规则**：

| 规则 | 说明 |
|------|------|
| EO 必须声明 `ContextType` | 需要读写 Context 的 EO（如 AiChatBus、AutomationBus）必须指定 |
| 只读访问不受限 | `getContextRead` 不做类型检查，任何 EO 可读任意 Context |
| Router 等纯转发 EO 不指定 | `ContextType = void`（默认），不操作 Context |
| 一个 EO 绑定一种 Context | EO 职责单一，不做多 Context 混合操作 |

**编译期 + 运行时双重保障**：

```
编译期：EO.ContextType → getContext<ContextType> → 返回类型确定，IDE 可补全
运行时：getContext 内部校验 (gtid >> 12) == TaskTypeOf<ContextType>
         不匹配 → 打 ERROR 日志 → 返回空 → EO 跳过消息并回空防重传
```

---

## 修订（2026-06-14）

### 修订动机

初版方案中 EO 用 `ContextType` 作为模板参数，但系统全链路中另一个核心概念 `TaskType` 已经承担了"业务类型标识"的职责：

- **消息中携带 `TaskType`**：接入层/内部 EO 发送 Task 建立请求时，消息中携带的是 `TaskType`（而非 `ContextType`），这是合理的——因为 `TaskType` 是业务概念，`ContextType` 是实现细节
- **GTID 中编码 `TaskType`**：GTID 的 `[11:6]` bit 存储的是 `TaskType` 枚举值
- **SessionMgr 按 `TaskType` 分配资源**：SessionMgr 收到请求后，按 `TaskType` 向 `TaskPool` 申请 GTID
- **`TaskPool` 按 `TaskType` 索引 `ContextManager`**：每种 `TaskType` 对应一个 `ContextManager`

在初版方案中，EO 作为这条链路的终点，其模板参数却是 `ContextType`——这与上游全部使用 `TaskType` 不一致，造成了概念上的断裂。运行时校验也需要反向从 `ContextType` 查 `TaskType`，增加了不必要的转换。

### 修订内容

**EO 模板参数从 `ContextType`（类型参数）改为 `TaskType`（非类型模板参数）。`ContextType` 改为通过编译期 traits 自动推导。`EoBase` 仍不感知此参数。**

```cpp
// EO 用 TaskType 声明自己处理哪种任务（与消息、GTID、SessionMgr 统一）
template <common::TaskType T>
class AiChatBus : public fw::EoBase<AiChatBus<T>>
{
    // 编译期推导 Context 类型（零开销）
    using ContextType = common::ContextTypeOf<T>;

    void handle(const SomeMsg& msg) {
        pool.getContext<ContextType>(gtid).useOrFailed(
            [&](ContextType& ctx) { /* 类型安全，IDE 可补全 */ },
            []()                   { /* 类型不匹配或 slot 无效 */ }
        );
    }
};

// 实例化
auto bus = system.spawn<AiChatBus<common::TaskType::AiChat>>();
```

**对比**：

| 维度 | 修订后（`TaskType`） | 初版（`ContextType`） |
|------|----------------------|----------------------|
| EO 声明 | `AiChatBus<TaskType::AiChat>` | `AiChatBus<AiChatContext>` |
| 语义 | "我处理 AiChat 任务" | "我读写 AiChatContext 结构" |
| 全链路标识 | 消息 → SessionMgr → GTID → EO **全部统一** | 消息用 TaskType，EO 用 ContextType，**概念断裂** |
| GTID 校验 | GTID 直接与 `T` 比较 | 需反向查 `ContextType → TaskType` |
| Context Type 获取 | `ContextTypeOf<T>` 编译期推导 | 直接就是模板参数 |

**映射机制**：`gen_code.py` 自动生成 `generated/TaskTypeTraits.hpp`，提供 `TaskType → ContextType` 的编译期映射：

```cpp
template <TaskType T> struct TaskTypeTraits;
template <> struct TaskTypeTraits<TaskType::AiChat>  { using ContextType = context::AiChatContext; };
template <> struct TaskTypeTraits<TaskType::Service>  { using ContextType = context::ServiceContext; };
template <> struct TaskTypeTraits<TaskType::Session>  { using ContextType = context::SessionContext; };

template <TaskType T>
using ContextTypeOf = typename TaskTypeTraits<T>::ContextType;
```

新增 context 类型只需添加 `.mt` 文件并运行脚本，映射自动生成。

---

## 最终规则

`TaskType` 是 EO 的**可选**模板参数。只有需要读写 Context 的 Business 层 EO 才声明，其他层 EO 保持普通 CRTP 形式即可。

### Business 层 EO（需要读写 Context）

```cpp
template <common::TaskType T>
class AiChatBus : public fw::EoBase<AiChatBus<T>>
{
    using ContextType = common::ContextTypeOf<T>;
    // ... 通过 pool.getContext<ContextType>(gtid) 读写 Context
};
```

### 其他层 EO（路由、Session 管理、接入转发等）

```cpp
// 无 TaskType 参数，普通 CRTP
class SessionMgr : public fw::EoBase<SessionMgr>
{
    // 不操作 Context，无需 TaskType
};

class Router : public fw::EoBase<Router>
{
    // 纯转发，不操作 Context
};
```

| 规则 | 说明 |
|------|------|
| Business 层 EO 声明 `TaskType` | 需要读写 Context 的 EO 通过模板参数绑定 TaskType |
| 其他层 EO 不声明 | SessionMgr、Router、接入层 EO 等无需 TaskType，保持普通 CRTP |
| ContextType 编译期推导 | `using ContextType = ContextTypeOf<T>`，IDE 补全完全可用 |
| 只读访问不受限 | `getContextRead` 不做类型检查，任何 EO 可读任意 Context |
| 一个 EO 绑定一种 Task | 对应一种 Context，职责单一 |

**编译期 + 运行时双重保障**（仅对声明了 TaskType 的 EO）：

```
编译期：EO 的 TaskType T → ContextTypeOf<T> → getContext<ContextType> → 返回类型确定，IDE 可补全
运行时：GTID 中 [11:6] 位编码 TaskType，getContext 校验 GTID 中的 TaskType == T
         不匹配 → 打 ERROR 日志 → 返回空 → EO 跳过消息并回空防重传
```

---

## 修订三（2026-06-14）：按目录约定 Context 的 GTID Category

### 修订动机

`TaskType` 枚举值编码了 `(Category << 6) | subType`（见 ADR-0008 修订），但 `.mt` 定义文件中没有声明所属 Category。需要一种机制让脚本自动知道某个 Context 属于哪个 Category。

### 决策：目录即分类

在 `src/common/context/` 下新增三个子目录，**目录名即 Category**：

```
src/common/context/
├── ConfigEntry.mt          ← 共享 struct（无 Category）
├── DeviceInfo.mt           ← 共享 struct（无 Category）
├── systemContext/          ← Category = System (0x0)
│   └── ServiceContext.mt
├── userContext/            ← Category = User   (0x7)
│   ├── AiChatContext.mt
│   └── SomeStruct.mt        ← 仅 User 分类使用的 struct
└── otherContext/           ← Category = Other  (0xC)
    └── SessionContext.mt
```

- **`context` 类型的 `.mt` 必须放在 `xxxContext/` 子目录中**（tidy 将检查此项）
- **`struct` 类型**：与某个 Category 绑定的 struct 放在对应的 `xxxContext/` 下；跨 Category 共用的 struct 放在 `context/` 根目录
- `gen_code.py` 根据 `.mt` 文件所在目录自动分配 Category，生成带 Category 编码的 `TaskType` 枚举值

> **注意**：`.mt` 文件的目录路径**仅用于区分 Category 分类**，不影响生成文件的输出路径。所有 `context/` 下的 `.mt`（含子目录）生成的头文件统一输出到 `generated/context/` 平级目录中。例如 `otherContext/DeviceInfo.mt` → `generated/context/DeviceInfo.hpp`。

### Include 规则

| include 写法 | 解析位置 | 用途 |
|-------------|---------|------|
| `include "xxx.mt"` | 当前 `.mt` 文件所在目录 | 同目录下的 struct 或 context |
| `include "context/xxx.mt"` | `src/common/context/` | 跨目录引用公共 struct |

示例：`otherContext/SessionContext.mt` 引用同目录的 `DeviceInfo`（Category 专属 struct），或引用公共 `ConfigEntry`（跨目录）：

```
include "DeviceInfo.mt"          ← 同目录引用

context SessionContext
    DeviceInfo deviceInfo
    ...
```

```
include "context/ConfigEntry.mt" ← 跨目录引用公共 struct

context SomeContext
    ConfigEntry config
    ...
```

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 运行时动态类型 | 无编译期保障，错误发现晚，IDE 无补全 |
| 每个 EO 内部硬编码 Context 类型 | 模板参数统一声明更简洁，且不依赖 ADL / include 顺序 |
| 初版 ContextType 方案（已采纳后修订） | 全链路概念不统一：消息用 TaskType，EO 却用 ContextType。修订后改为 TaskType |
| 用 TaskType 运行时替代 Context 类 | 丢失字段信息，无 IDE 补全和类型安全 |

---

## 修订记录

| 日期 | 变更 |
|------|------|
| 2026-06-12 | 初版决策：EO 模板参数为 `ContextType`（类型参数），直接绑定 Context 类 |
| 2026-06-14 | 修订一：改为 `TaskType`（非类型模板参数），通过 `TaskTypeTraits` 编译期推导 `ContextType`。目的：与消息、GTID、SessionMgr 中的 TaskType 统一全链路标识，消除概念断裂。新增 `generated/TaskTypeTraits.hpp` 由 `gen_code.py` 自动生成 |
| 2026-06-14 | 修订二：明确 `TaskType` 为可选参数——仅 Business 层 EO 需要声明，其他层 EO（SessionMgr、Router 等）保持普通 CRTP。`EoBase` 始终不感知此参数 |
| 2026-06-14 | 修订三：Context `.mt` 文件按目录分类（`systemContext/` / `userContext/` / `otherContext/`），`gen_code.py` 根据目录自动分配 GTID Category。`TaskType` 枚举值编码 `(Category << 6) | subType`，不同 Category 可复用 subType 号。新增 include 规则：`include "context/xxx.mt"` 跨目录引用公共 struct |

## 影响

- Business 层 EO 通过 `TaskType` 非类型模板参数声明业务类型；其他层 EO 保持普通 CRTP，无需改动
- `EoBase` 无需修改——它始终只接受 `Derived` 一个模板参数
- `TaskTypeTraits.hpp` 由脚本生成，新增 context 无需手动维护映射
- `TaskPool::getContext` 的模板参数仍为 ContextType，由 Business 层 EO 内部通过 `ContextTypeOf<T>` 推导
- Context `.mt` 文件必须放在对应 Category 子目录下（`systemContext/` / `userContext/` / `otherContext/`）
- 与 Category 绑定的 struct `.mt` 可放在对应的 `xxxContext/` 下，跨 Category 共用的 struct 放在 `context/` 根目录，通过 `include "context/xxx.mt"` 跨目录引用
- tidy 规则仅检查 `context` 类型必须在 `xxxContext/` 子目录中，不约束 `struct` 位置

## 待办

- [ ] 添加 clang-tidy 规则：约束 `getContext<ContextType>` 调用处的 `ContextType` 必须等于 `ContextTypeOf<当前EO的TaskType>`，禁止手动指定其他类型绕过规则
- [ ] 添加 tidy 规则：检查 `context` 类型 `.mt` 文件必须位于 `xxxContext/` 子目录中
