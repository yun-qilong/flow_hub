# ADR-0009：GTID Context 访问规则、物理存储与映射表同步协议

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-01 | 韵启龙 |

> **修订（2026-07-30）**：
> - **架构变化**：Session 层 D 面从被动透传升级为主动编排。原 SessionData 仅做消息转发，不存在"Session 层业务状态"的概念，因此原规则"仅 Business 层 D 面 EO 可读写 Context"成立。引入编排器（AiAgora）后，Session 层被赋予全新职责——一个 Session Task 通过编排器将复杂任务拆解为多个 Bus 层子任务执行，编排过程本身需要维护状态（子任务 GTID 列表、等待位图、辩论轮次、上下文历史等）。Session 层从此有了自己的 Context 需求。
> - **决策逻辑**：如果维持"仅 Bus 层可读写"的旧规则，编排器要么把 Session 级状态塞进 Bus Context（语义错乱），要么在自身成员变量中维护（多 Session 并发时互相覆盖）。最干净的方案是承认分层事实——**每层管理自己 TaskType 的 Context**。这并非推翻旧规则，而是架构演进后的自然外推：旧规则的本质是"谁执行任务谁持有 Context"，新架构下 Session 层也在执行编排任务。
> - **边界约束**：统一入口组件（SessionDispatcher、Router）仍是纯路由基础设施，不持有 Context 访问权——它们不执行任务，只做路由，与旧规则精神一致。跨层操作通过消息委托完成，不设直接访问例外。
> - 修订依据：FT0002-B 设计。
>
> **修订（2026-06-15，更新于 2026-07-01）**：
> - §5（虚拟 ID 分配）被 [ADR-0011](../adr/0011-gtid-routing-key.md) 废弃——GTID 的 TaskType 位直接作为 Router 路由键，不再需要 SessionMgr 向 BusinessMgr 单独申请虚拟 ID。
> - §4 的映射表概念由 [ADR-0019](../adr/0019-router-route-table.md) 细化为定长数组 `routeTable_[TaskType] = EoAddress` 的实现，路由表条目值可动态更改以支持未来负载均衡和热备切换。
> - §6（Adapter 地址下发）的具体链路由后续 ADR 覆盖。
> - 其余部分（Context 物理存储、读写权限、并发控制）保持不变。

---

## 背景

ADR-0008 确定了 GTID 作为统一任务标识。在此基础上，需要明确以下三个紧密关联的问题：

1. **Context 归谁读写、存在哪里**：每个 GTID 对应一组业务上下文（Context），需要确定其物理存储位置和读写权限。
2. **映射表归谁维护**：虚拟 ID → 物理 EO 地址的映射表是 Router 转发的基础，也是 BusinessMgr 负载均衡的依据。需要确定谁持有映射表、如何同步。
3. **地址如何下发**：Service Layer 的 Adapter 地址需要让 Business Layer 知道，下发链路是什么。

---

## 决策

### 1. Context 物理存储：TaskPool

- 在 `main()` 中实例化一个 `TaskPool` 对象，以引用方式传给所有需要访问 Context 的组件。
- `TaskPool` 内部使用**静态内存池**（与系统整体内存模型一致），按 GTID 索引定位独立的 slot。
- `TaskPool` 同时提供 GTID 的生成与回收接口。

```
main()
  └─ TaskPool taskPool;             // 唯一实例
       ├─ 传给 SessionMgr           // GTID 创建/销毁
       ├─ 传给 Business Layer EO    // Context 读写
       └─ 传给 BusinessMgr          // 负载均衡可能需查询
```

### 2. Context 读写权限：分层管理（2026-07-30 修订）

> **原规则**（2026-06-01）：仅 Business 层 D 面 EO 有权读写 Context。此规则在 Session 层仅有透传 EO（SessionData）时成立，但引入 Session 层编排器（AiAgora）后不再适用——Session 层有了自己的 Context，需要明确分层权限。

**分层读写权限**：

| 角色 | Session Context | Bus Context | System Context |
|------|:---:|:---:|:---:|
| Session 层 D 面业务 EO（如 AiAgora） | 读+写 | 不可访问 | — |
| Bus 层 D 面业务 EO（如 AiChatBus） | 不可访问 | 读+写 | — |
| SessionDispatcher（Session 统一入口） | 不可访问 | 不可访问 | — |
| Router（Bus 统一入口） | 不可访问 | 不可访问 | — |
| SessionMgr（C 面） | 创建+销毁 | 创建+销毁 | — |

**关键约束**：
- 统一入口组件（SessionDispatcher、Router）是**纯路由基础设施**，不持有 Context 访问权。它们只做消息路由（按 `sessionTaskId` 或 `busTaskIds` 查表 delegate），不读写任何 Context。
- 跨层 Context 操作通过**消息委托**完成。例：Session 层编排器通过 `AiChatConfigReq` 消息委托 Bus 层 EO 初始化 Bus Context，不直接写入。
- 不设例外规则，不做编译期访问控制（如 `friend`/token）。
- System Context 权限暂不定义——当前无 System 类型 Task，等有需求时再追加。

**原子性规则**（不变）：EO 在处理一条消息的过程中，所有 Context 修改必须与该消息的处理原子绑定。

### 3. Context 并发控制

- **业务层保证**：同一 GTID 的 Context 同一时刻只被一个 Business EO 访问。
- **即便多 EO 同时访问不同 GTID**：`TaskPool` 按 GTID 定位独立 slot，不同 slot 之间无共享数据。
- **Cache Line 隔离**：可能被同时访问的不同 Context slot 强制分配在不同 cache line 上，避免伪共享（false sharing）导致的 cache 弹跳。

### 4. 路由表：TaskType → EO 地址

> **修订（2026-07-01）**：本节内容已根据 ADR-0019 和当前 Router 实现更新。核心变化——不再使用独立的"虚拟 ID"作为中间层，TaskType 直接作为路由表下标。

Router 持有一张定长路由表（ADR-0019），将 TaskType 映射到处理该类型任务的 EO 地址：

```cpp
static constexpr uint16_t kRouteTableSize = 1024;
std::array<fw::EoAddress, kRouteTableSize> routeTable_{};

fw::EoAddress getTargetEoAddress(uint16_t gtid) const {
    return routeTable_[gtid >> 6];  // TaskType = GTID 高 10 位
}
```

| 维度 | 说明 |
|------|------|
| 索引 | `GTID >> 6`，即 TaskType（10 bits，最多 1024 种） |
| 查表 | 单次数组下标，O(1)，零哈希开销 |
| 值 | `EoAddress`——处理该 TaskType 的 EO 的物理地址 |
| 虚拟性 | `routeTable_[taskType]` 的值可动态更改，指向不同 EO 实例。这是负载均衡和热备切换的机制基础——只需更新路由表条目，GTID 和 TaskType 不变，上游完全无感 |

路由表通过 Config/Reconfig 协议管理（详见 ADR-0019）：
- **RouterConfigReq**：setup 阶段全量下发路由表
- **RouterReconfigReq**：运行时增量更新单个 `TaskType → EoAddress` 映射

BusinessMgr 可在未来根据负载或 EO 健康状态，通过 Reconfig 调整路由表条目，实现同类型 EO 间的动态调度。

### 5. 新 Task 建立（⚠️ 已废弃，保留供参考）

> **修订（2026-07-01）**：本节描述的独立"虚拟 ID 分配"流程已被废弃。当前设计中 GTID 的 TaskType 位直接作为 Router 路由表下标（`routeTable_[GTID >> 6]`），不再需要 SessionMgr 向 BusinessMgr 请求分配独立的虚拟 ID。SessionMgr 分配 GTID 时编码 TaskType，Router 直接查表找到目标 EO。

SessionMgr 创建 task 时，向 BusinessMgr 请求虚拟 ID：

```
SessionMgr ──SetupReq(业务类型)──▶ BusinessMgr
                                       │
                                       │ 查映射表 + 负载数据
                                       │ 选择负载最低的实例
                                       │ 返回虚拟 ID
                                       │
SessionMgr ◀──SetupResp(虚拟ID)───     │
```

BusinessMgr 根据自身维护的映射表和负载数据，返回合适的虚拟 ID。多个虚拟 ID（如 `0x0001`、`0x0002`）可映射到同一物理 EO（初期），扩展多实例后不同虚拟 ID 可映射到不同实例。

### 6. Adapter 地址下发链路

Service Layer 的 Adapter 地址需要让 Business Layer（主要是 Router）知道，用于业务 EO 向服务层发送消息：

```
ServiceMgr ──(Adapter地址)──▶ BusinessMgr ──(Reconfig)──▶ Router
```

- ServiceMgr 是 Adapter 地址的 source of truth。
- BusinessMgr 收到后通过已有的 Reconfig 机制同步给 Router。
- **动态更新**：Adapter 崩溃重启后地址变更，ServiceMgr 主动沿同样链路推送更新。

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| Context 存在 SessionMgr 手里，EO 通过消息访问 | 每次读写都要跨 EO 发消息，延迟过高 |
| Context 存在每个 EO 本地缓存 | EO 崩溃后缓存丢失；多实例间一致性难维护 |
| 映射表只存 Router，BM 每次查询 Router | BM 发送消息查询再等回复，增加 task 建立延迟；BM 无法独立做负载均衡 |
| 映射表只存 BM，Router 每次查 BM | Router 处于消息转发热路径，每次查 BM 不可接受 |
| BM 和 Router 同时修改映射表（无 ACK） | 无法保证一致性，可能出现 BM 认为已切换但 Router 仍用旧地址 |

---

## 影响

- 新增 `TaskPool` 类，在 `main()` 中实例化，引用传递
- `SessionMgr` 不再直接操作 Context，改为通过 `TaskPool` 管理 slot 生命周期
- `BusinessMgr` 新增映射表维护逻辑和 Reconfig 消息处理
- `Router` 新增映射表副本维护和 Reconfig 消息处理
- Service Layer Adapter 地址变更时需通知 ServiceMgr，触发地址下发链路
