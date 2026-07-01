# ADR-0011：GTID 替代虚拟 ID 作为路由键，Router 定位为层内设施

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-15 | 韵启龙 |

> **修订（2026-07-01）**：§3 中 `sourceAddress` 字段已被 [ADR-0024](../adr/0024-head-accesstype-reuse.md) 移除。`accessType` 承担回程路由、fan-out 位图索引、源 Adapter 排除三重职责。其余内容（GTID 作为路由键、Router 定位为 Business Layer 内部设施、入向经 Router 出向直连）保持不变。

---

## 背景

ADR-0008 确定了 GTID 作为统一任务标识。ADR-0009 确定了映射表维护和虚拟 ID 分配机制。

原设计中存在两级映射：`GTID → 虚拟 ID → 物理地址`。其中虚拟 ID（如 `0x0001`）作为独立概念存在于消息体中（`SchedulerAddress` 字段），用于 Router 查表。

经分析发现：

1. GTID 的 `[11:6]` bit 已经编码了 `TaskType`，与虚拟 ID 携带的信息完全相同（都是标识"消息应发给哪种业务 EO"）
2. 同一个 GTID 的所有消息（请求和应答）天然应路由到同类型 EO
3. "双地址"（`RoutineAddress` + `SchedulerAddress`）增加了消息体和概念的复杂度
4. 原设计中 Router 被定位为"所有跨层 D 面消息的中转枢纽"，但这一职责与它作为 Business Layer 内部 EO 的位置不匹配

## 决策

### 1. 消除虚拟 ID 概念，GTID 即路由键

Router 直接从 GTID 提取 `TaskType` 位作为路由键，一级映射 `GTID → 物理地址`：

```
Router 查表键 = GTID >> 6（即 TaskType 字段）

GTID 0x7001 → TaskType 0x01C0 (AiChat) → 查表 → AiChatBus 实例地址
```

**热备切换**：Router 更新 `TaskType → 活跃实例地址` 映射。GTID 不变，上游无感。

**负载均衡**：同一 `TaskType` 维护多个实例。Router 从实例池中任选一个投递。因 Context 存储在共享的 TaskPool 中，不同实例处理同一 GTID 的消息无冲突——ADR-0009 的原子性规则保证同一时刻仅一条消息在处理。无需分片或亲和性绑定。

**SessionData 负担**：零。GTID 在会话创建时由 SessionMgr 分配，SessionData 本就持有。

### 2. Router 定位为 Business Layer 内部设施

Router 是 Business Layer D 面的一个 EO。它只负责本层 EO 的地址映射（热备/负载均衡）。

**仅发往 Business D 面 EO 的消息经 Router。从 Business D 面 EO 发出的消息物理直连。**

```
入向（经 Router）：
  SessionData ──sendTo(Router)──▶ Router ──delegateTo──▶ AiChatBus

出向（直连）：
  AiChatBus ──sendTo(SessionData)──▶ SessionData（物理直连）
  AiChatBus ──sendTo(ServiceGateway)──▶ ServiceGateway（物理直连）
```

**若其他层将来引入热备**，应在各自层内增设自己的 Router，而非扩展当前 Router 的职责。

### 3. 统一消息头（⚠️ 修订：`sourceAddress` 已由 ADR-0024 移除，`accessType` 承担回程路由职责）

> **以下为原始决策内容。其中 `sourceAddress` 字段已被 [ADR-0024](../adr/0024-head-accesstype-reuse.md) 移除。当前消息头字段见 README §3.3（uid + gtidList + accessType + appType + sessionFlags）。**
> **本节中关于 GTID 作为路由键、Router 提取 TaskType 位查表的核心逻辑仍然有效。**

所有消息统一携带两个头部字段：

| 字段 | 类型 | 含义 |
|------|------|------|
| **GTID** | `uint16_t` | 统一任务标识，Router 提取 TaskType 位路由 |
| **sourceAddress** | `EoAddress` | ~~回复地址，接收方 `sendTo(msg.sourceAddress, resp)` 即可~~（⚠️ 已于 2026-07 由 ADR-0024 移除） |

**`sourceAddress` 填入规则**（⚠️ 已随 sourceAddress 移除而废弃，仅供参考）：

| 发送方 | 消息方向 | sourceAddress 填什么 |
|--------|---------|---------------------|
| SessionData 等 | → Business D 面 EO（经 Router） | `myAddress()`（自己的地址） |
| Business D 面 EO | → 外部（直连） | Router 地址 |

接收方无需判断该走 Router 还是直连——发送方已通过 `sourceAddress` 替它决定了。

### 4. `requestThen` 适用于不经 Router 的通信

CAF 的 `request().then()` 机制（封装为 `EoBase::requestThen`）的 reply 路径绕过中间人，因此不适用于经过 Router 的通信。

**判定标准**：消息路径上是否经过 Router。

| 场景 | 用 `requestThen`？ |
|------|:---:|
| C 面直连（如 SessionMgr → BusinessMgr） | ✅ 推荐 |
| D 面同层/邻层直连（如 Adapter → ServiceGateway） | ✅ 可用 |
| 经 Router 的 D 面流转 | ❌ 用 Fire-and-Forget + `sourceAddress`（⚠️ sourceAddress 已移除，现用 accessType + Gateway adapterTable_ 回程） |

### 5. Router 使用 `delegate()` 零拷贝转发

Router 只读 GTID 提取 TaskType 查表，不修改消息体。使用 CAF 的 `delegate()` API 零拷贝转发，不申请新内存。

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 保留虚拟 ID 作为独立字段 | GTID 已编码 TaskType，两级映射无额外信息增益 |
| Router 作为全局跨层路由枢纽 | Router 是 Business Layer 内部 EO，管全局事违反分层原则 |
| 所有跨层消息都经 Router | 非 Business 层的 EO 无地址映射需求，强行中转增加 2 次调度开销 |
| Router 修改消息体填入虚拟 ID | 破坏零拷贝，增加内存分配热点 |

---

## 影响

- 消息体不再有 `SchedulerAddress` / `RoutineAddress` 双地址，改为 `GTID` + `sourceAddress`（⚠️ sourceAddress 后续由 ADR-0024 移除）
- ADR-0009 §4（映射表）、§5（虚拟 ID 分配）需相应修订
- README §3.3（消息）、§6.1（路由规则）、§6.2（Router 与双地址）已更新（⚠️ README 后续又经 checklist 和 ADR-0024 进一步更新）
- Router 实现简化：查表键从虚拟 ID 改为 `GTID >> 6`
- Business Layer 内非 Router EO 的 `myVirtualId` 成员不再需要
