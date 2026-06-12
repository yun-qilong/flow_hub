# ADR-0010：EO 强制声明 ContextType 模板参数

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-12 | 韵启龙 |

---

## 背景

ADR-0009 确定了 Context 存储于 `TaskPool` 中，EO 通过 GTID 访问 Context。但仅靠 GTID 无法在编译期建立 EO 与 Context 类型的绑定关系。

当前 `EoBase` 只接受一个模板参数 `Derived`（CRTP），不约束 EO 能操作哪种 Context。这意味着：

1. 任何 EO 理论上可以传入任意 GTID 请求任意类型的 Context，错误只能在运行时暴露
2. EO 代码中无法从类型系统获知"我的 Context 是什么"，IDE 补全和重构均无感知
3. 新增 Context 类型时，编译器不会提示哪些 EO 需要适配

## 决策

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

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 运行时动态类型 | 无编译期保障，错误发现晚，IDE 无补全 |
| 每个 EO 内部硬编码 Context 类型 | 模板参数统一声明更简洁，且不依赖 ADL / include 顺序 |
| 用 TaskType 枚举替代 Context 类 | TaskType 不携带字段信息，无发提供 IDE 补全和类型安全 |

## 影响

- EO 通过自身模板参数声明 `ContextType`，`EoBase` 无需修改
- 纯转发 EO（如 Router）不声明此参数
- 新 EO 在定义时即绑定 ContextType
- `TaskPool::getContext<ContextType>` 的模板参数与 EO 模板参数一致
- README EO 相关描述需更新

## 待办

- [ ] 添加 clang-tidy 规则：约束 `getContext<ContextType>` 调用处的 `ContextType` 必须等于所在 EO 类自身的模板参数，禁止手动指定其他类型绕过规则
