# ADR-0022：BatchFanOut/FanOutMsg 两级上行广播机制

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-01 | 韵启龙 |

---

## 背景

当 Business EO（如 AiChatBus）处理完请求后，产生的 ACK 或 AI 回复需要广播给同一用户在所有接入方式（Adapter）上的连接，以实现多端消息同步。

**旧方案（ADR-0013）** 采用 Gateway 出向预埋 GTID 列表的方式实现 fan-out——Gateway 在转发请求给外部 Adapter 时，将额外 GTID 嵌入消息，Adapter 入向时一并打包发给 Router 拆解路由。该方案适用于**下行请求的响应需要抄送多个 Business EO** 的场景（如传感器读数抄送 DataManager），但不适用于**上行消息广播到所有 Adapter** 的场景——后者是"同一份数据发给多个同层目标"，而非"同一份数据发给多个下游 EO"。

需要一个专门的机制处理上行广播：一份消息，发给多个 Adapter。

## 决策

### 两级消息格式

**第一级：BatchFanOut**（SessionData → Gateway，层间批量消息）

```
BatchFanOut {
    head:      CommonHead,    // uid、gtidList、accessType、sessionFlags
    payload:   Payload,       // 业务消息体（如 ACK 或 AI 回复），零拷贝引用
    targets:   uint64_t,      // 目标 AccessType 位图，直接取自 userAccessBitset[uid]
}
```

- `targets` 采用 `uint64_t`——`MAX_ACCESS_TYPES` = 64 正好填满 64 位；`userAccessBitset` 本身就是 `uint64_t`，直接拷贝零转换
- `head.accessType` 保持原始值不变（即源 Adapter 的 AccessType），Gateway 不重写
- SessionData 不修改 `targets`——源排除由 Adapter 自行完成

**第二级：FanOutMsg**（Gateway → Adapter，单播消息）

```
FanOutMsg {
    head:    CommonHead,    // 与 BatchFanOut.head 相同，accessType 保持源值
    payload: Payload,       // 与 BatchFanOut.payload 相同，零拷贝或引用
}
```

- 独立消息类型——Adapter 据此区分广播消息与普通下行响应
- 不携带 `targets`——已是单播

### 三级职责链

```
SessionData                 Gateway                    Adapter
    │                          │                          │
    │  收到上行消息（ACK/回复）   │                          │
    │  uid → userAccessBitset   │                          │
    │  组装 BatchFanOut ───────▶│                          │
    │                          │  遍历 targets 置位的       │
    │                          │  AccessType               │
    │                          │  查 adapterTable_         │
    │                          │  构造 FanOutMsg ─────────▶│
    │                          │                          │  userToConn_[userId]
    │                          │                          │  → 发送给前端
```

**SessionData 职责**（不做的事）：
- 不做 targets 过滤
- 不做源 adapter 排除
- 不迭代分发——Gateway 负责拆解
- 保持"只写位图、只读位图"的单一数据面角色

**Gateway 职责**：
- 收到 `BatchFanOut` → 遍历 `targets` 中每个置位的 AccessType
- 查 `adapterTable_[accessType]` 获取 EoAddress
- `adapterTable_[accessType]` 为空（该 AccessType 无 adapter 注册）→ 跳过，不报错
- 构造 `FanOutMsg` → 逐 adapter 发送
- **不做源排除**——全部发送，排除是 Adapter 的业务逻辑

**Adapter 职责**：
- 收到 `FanOutMsg` → 读 `head.uid` → `userToConn_[userId]` 查连接
- 无连接 → 静默丢弃（用户在该 adapter 上未登录或已断连）
- 有连接 → 据 `head.accessType` 决定发给前端的消息类型：
  - `head.accessType == self` → 发"消息送达"通知（源端确认）
  - `head.accessType != self` → 发"消息同步"通知（其他端展示）

### 源 Adapter 排除策略：Adapter 自判

SessionData 和 Gateway 均不做排除。所有 adapter 收到 `FanOutMsg`。源 adapter 据 `head.accessType == myAccessType` 识别自身。

**理由**：
1. 源 adapter 需要 ACK 确认更新自身状态（如清空重发缓冲区、更新 UI 为"已发送"）
2. Gateway 不重写 head，`accessType` 天然就是源
3. SessionData/Gateway 保持零决策

### 边界情况

| 情况 | 行为 |
|------|------|
| `targets` 为空（`userAccessBitset[uid]` 全零） | SessionData 不发送 `BatchFanOut`，打 log |
| 所有 `targets` 对应 adapter 均未注册 | Gateway 静默丢弃，打 log |
| 目标 adapter 上 user 无连接 | Adapter 静默丢弃，打 log |

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 复用 ADR-0013 Gateway 出向预埋 | ADR-0013 是下行 fan-out（一份响应抄送多个 Business EO），上行 fan-out 是"一份数据发给多个同层 Adapter"，拓扑不同。ADR-0013 仍适用于 Service 层 fan-out |
| SessionData 逐 adapter 迭代发送 | SessionData 需要知道 Gateway/adapter 拓扑，引入了不必要的耦合 |
| SessionData 排除源 adapter（修改 targets） | SessionData 应保持零决策；且源 adapter 也需要收到 ACK |
| Gateway 排除源 adapter | Gateway 不应读消息内容做业务判断 |
| Adapter 透传 ACK 给前端 | Adapter 应负责区分"送达"和"同步"，不能无脑透传 |
| `targets` 用 `StaticBitMap` | 当前 64 上限 `uint64_t` 更简洁；扩展上限时随常量改即可 |

## 影响

- **新增消息类型**：`BatchFanOut`（SessionData→Gateway）、`FanOutMsg`（Gateway→Adapter）。均定义在 `common/message/`。
- **SessionData**：新增上行消息到达时的 fan-out 组装逻辑（读位图 → 组装 BatchFanOut → 发 Gateway）。
- **Gateway**：新增 `BatchFanOut` 消费者的拆解分发逻辑；复用现有 `adapterTable_`。
- **Adapter**：新增 `FanOutMsg` 处理逻辑（查连接 + 区分送达/同步）。
- **与 ADR-0013 的关系**：ADR-0013（Gateway 出向预埋 GTID 列表）仍适用于 Service 层下行 fan-out（传感器数据抄送 DataManager 等），不被取代。ADR-0022 是上行广播的独立机制。
- **消息头字段**：`head.accessType` 不再被 Gateway 重写，保持原始值作为源标识。

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-07-01 | 初稿，采纳 |
| 2026-07-02 | 方案简化：废弃 `BatchFanOut` + `FanOutMsg` 两级消息，改为 `UserHead.targets` 单字段方案 |

### 2026-07-02：废弃两级消息，改为 UserHead.targets

**原方案**：SessionData 组装 `BatchFanOut{head, payload, targets}` → Gateway 拆解为 `FanOutMsg{head, payload}` → 逐 Adapter 发送。需新增两个消息类型。

**简化后**：`UserHead` 增加 `uint64 targets` 字段（默认 0 = 不 fan-out）。SessionData 填入位图后原消息直传 Gateway，Gateway 遍历 targets 逐 Adapter 转发。零新消息类型。

**简化理由**：
1. `BatchFanOut` 和 `FanOutMsg` 的 payload 就是 BusinessEO 发出的原始消息体——包装再拆包是多余的
2. CAF 消息系统不支持模板化消息类型，`BatchFanOut` 无法承载不同 payload 类型
3. `targets` 位图描述的是"该用户当前在哪些 adapter 上"——与 `uid` 同为 User 维度信息，放在 `UserHead` 中语义合理
4. SessionData 迭代发送 N 条消息（N = 活跃 adapter 数，通常 1~3），开销可接受

**Gateway fan-out 实现**：私有模板 `fanOutToAdapters<Msg>(msg)` 统一处理——读 `head.targets`、清零、遍历置位 bit 查 `adapterTable_`、逐 adapter 拷贝发送。

**Adapter 侧**：不再接收 `FanOutMsg` 统一类型，改为直接处理各业务消息类型（`AiChatMsgAck`、`AiChatBusinessResp` 等），按 `head.accessType` 区分源/同步。

**原决策中仍然有效的部分**：
- 源 Adapter 排除策略：Adapter 自判（`head.accessType == myAccessType`）
- SessionData/Gateway 不做源排除，保持零决策
- 边界情况处理（targets 为空、adapter 未注册、user 无连接）
- `targets` 用 `uint64_t` 而非 `StaticBitMap`
