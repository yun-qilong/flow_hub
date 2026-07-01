# 接入层与会话层 — 设计决策路线图

> **一轮一轮确认。** 前一轮所有项打勾后，再根据已锁定结论丰富下一轮细节。
> 后轮只列大纲和关键问题，不写细节，等轮到该轮时再展开。
>
> 背景：ADR-0008/0010/0011/0014/0017/0018/0020

---

## 路线总览

```
Round 1  基础约束    →  类型定义、隔离规则、adapter 粒度
Round 2  数据结构    →  所有静态表、持有者、大小
Round 3  SessionData →  模式与职责
Round 4  消息下行    →  请求路径，不做 fan-out
Round 5  消息上行    →  ACK/回复广播路径
Round 6  Fan-out    →  BatchFanOut/FanOutMsg 格式
Round 7  C 面生命周期 →  注册/登录/登出/注销
Round 8  D 面生命周期 →  新建/删除会话 + 消息同步
Round 9  跨切面      →  flag、超时、常量定稿
```

---

## Round 1 — 基础约束与类型体系

> 确认所有概念层面的约束。此轮不涉及具体数据结构。
>
> **命名约定**：`uid`（uint16_t）= `(userId << 8) | appType`。`userId`（uint8_t）为用户身份标识（username 级别），`appType`（uint8_t）为 AppType 枚举值。消息头中携带 `uid`，SessionMgr 的 `usernameToId_` 映射到 `userId`。

### 1.1 三层 Type 定义

- [x] **AccessType**：Adapter 唯一标识。每个 (AppType × 连接方式) 组合对应一个 AccessType 值。
  - 例：`AiChatWeb`、`AiChatCLI`、`SmartHomeWeb`、`SmartHomeApp`。
  - 作为 `UserHead` 字段随消息全程流转。
  - 实际使用者：Access 层（Adapter 填入自身 AccessType，Gateway 查 `adapterTable_` 路由）+ SessionData（维护 `userAccessBitset`）。
  - `enum class AccessType : uint8_t`，位置 `common/type/`。

- [x] **AppType**：客户端功能集合。"做什么"（AiChat / SmartHome / …）。`enum class AppType : uint8_t`，位置 `common/type/`。
  - **前端**：一个连接对应一个 AppType，声明即可。
  - **Adapter**：一个 Adapter 处理一种特定 AppType 的事务。同一 AppType 可因连接方式不同有多个 Adapter（如 `AiChatWebAdapter`、`AiChatAppAdapter`）。有些行为跟 TaskType 无关、只跟 AppType 有关——比如 AI 对话类 App 需要多端聊天记录同步，每个 request 都要 Business 层回复 ACK。我们不希望 Business 层去判断 AppType 来区分行为，而是由 Adapter 在发出消息时，把这类 AppType 特有的要求编码进 `sessionFlags`（如 `needsAck`），Business EO 只需读标志执行即可。Adapter 编译期就知道自身 AppType，这些标志编译期填入，开发者不会遗漏。
  - **Gateway**：只按 AccessType 区分 adapter，不关心 AppType。
  - **SessionMgr**：在新 task 建立申请（`NewSessionReq`）时，据 AppType 所包含的 TaskType 集合校验该 uid 是否有资格建立此 TaskType——这是唯一闸门。通过后分配的 GTID 自然合法，登录时返回 GTID 列表无需再过滤。
  - **SessionData**：新 user 登录时据 `UserLogin` 中的 AppType 决定初始动作（如是否触发上下文同步）。后续业务数据消息都是 GTID 粒度，SessionData 只做透传，不据 AppType 区分行为。
  - **Business 层及以下**：AppType 不可见。登录后正常业务消息路径为 `adapter → Gateway → SessionData → Router → Business EO`，中间三层只转发，Business EO 收到的就是 Adapter 组织的原始消息。AppType 相关的特殊要求（如需要 ACK）已由 Adapter 编码为 `sessionFlags`，Business EO 只读标志行事，不需要知道 AppType。

- [x] **TaskType**：原子业务任务。"哪个任务"（SingleAiChat / MultiAiDiscussion / DeviceMonitor / …）。
  - GTID 高位编码 TaskType，全链路携带。
  - 可见范围：前端 ↔ 所有层。

### 1.2 类型可见性矩阵

| | AccessType | AppType | TaskType |
|---|---|---|---|
| 前端 | ❌ 不感知 | ✅ 声明 | ✅ 每个 Task 都有 |
| Adapter | ✅ 填入/透传 | ✅ 据此组织消息 | ✅ 透传（GTID 中） |
| Gateway | ✅ 查表转发 | ❌ 不感知 | ❌ 不感知 |
| SessionMgr | ✅ 回程用 | ✅ 拼 username key | ✅ 分配 GTID |
| SessionData | ✅ fan-out 位图 | ✅ 据以决策 | ✅ delegate |
| Router | ❌ 透传 | ❌ 不可见 | ✅ 按 TaskType 路由 |
| Business EO | ❌ 透传 | ❌ 不可见 | ✅ 执行 |
| Service | ❌ 透传 | ❌ 不可见 | ❌ 透传 |

### 1.3 核心约束

- [x] **无 super TaskType**：TaskType 正交，不存在组合多个 TaskType 的新 TaskType。融合业务 = 新 TaskType + 新 EO（调用公共工具类）。

- [x] **AppType 终止于 SessionData**：Business 层不可见。登录后正常业务消息为 GTID 粒度，Adapter→Gateway→SessionData→Router→BusinessEO 全链路仅转发，SessionData 不据 AppType 做行为差异；Business EO 收到的即 Adapter 原始组织。

- [x] **AppType 包含特定 TaskType 集合**：AppType 区前端类型，每个 AppType 包含一组正交 TaskType，本身不产生新语义。该集合由 SessionMgr 持有（编译期常量），用于 `NewSessionReq` 的闸门校验。

### 1.4 Adapter 粒度

- [x] **一个 Adapter = 一个 (AppType × 连接方式) 组合，对应一个 AccessType 值。**
  - Adapter 编译期硬编码自身 AppType 和 AccessType。AppType 特有的行为要求（如多端同步需 ACK）由 Adapter 编码进 `sessionFlags` 写入消息，Business EO 只读标志无需感知 AppType。编译期填入，开发者不会遗漏。
  - 例：`AiChatWebAdapter`（AccessType::AiChatWeb）、`AiChatAppAdapter`（AccessType::AiChatApp）、`SmartHomeWebAdapter`（AccessType::SmartHomeWeb）、`SmartHomeAppAdapter`（AccessType::SmartHomeApp）。
  - 每个 Adapter 内所有 client 属于同一 AppType，Adapter 无需做 AppType 过滤。
  - 同类连接方式的 Adapter 通过基类/组合共享底层代码。

### 1.5 uid 编码与隔离

- [x] **uid = uint16_t，编码 [userId:8][AppType:8]**。uid 自携带 AppType（uid & 0xFF），无需额外字段或查表。
  - MAX_USERS = 64（userId 上限），MAX_APP_TYPES = 64。
  - 同一真实用户在不同 AppType 下使用相同 username → 同一 userId（高 8 位同），不同 AppType（低 8 位不同）→ 不同 uid。
  - usernameToId_ 简化为 username → userId（uint8_t），无需字符串拼接。
  - UserRecord 为 array[64][64] = 4096 项二维表，按 [userId][appType] 索引。
  - userAccessBitset 按 uid 直接索引，不同 AppType 的 uid 天然落在不同段，fan-out 自动隔离。

### 1.6 Adapter 内连接约束

- [x] **同一 Adapter 内，一个 user 最多一个连接。不允许多开。**
  - Adapter 无需维护 user→多 client 的映射。
  - `userToConn_[userId]` 直接 O(1) 查连接（adapter 内所有连接 AppType 相同，用 userId 索引即可，64 项）。
  - 砍掉：`clientId`（连接方式标识直接返回 `uid + appType + accessType` 即可定位）、`userToClients_` bitset。

### 1.7 消息头约定

- [x] 所有内部消息统一携带 `uid` + `gtidList`。
- [x] Adapter 发出消息必须携带 `accessType`（填自身 accessType）。
- [x] 控制面请求（注册/登录）额外携带 `appType`（尚无 uid 时）。
- [x] 不再携带 `sourceAddress`（架构拓扑固定）。

---

## Round 2 — 数据结构

> 确认所有静态表的结构、持有者、大小和常量。

### 2.1 常量汇总

> 先定常量，后续各节引用。

| 常量 | 值 | 说明 |
|------|----|------|
| `MAX_USERS` | 64 | userId（uid 高 8 位）上限 |
| `MAX_APP_TYPES` | 64 | AppType 枚举值上限 |
| `MAX_UID` | 65536 | uid 全空间（= MAX_USERS × MAX_APP_TYPES） |
| `MAX_ACCESS_TYPES` | 64 | AccessType 枚举值上限 |
| `MAX_CLIENTS_PER_ACCESS` | 64 | 每个 Adapter 最大连接数 |
| `MAX_GTIDS_PER_USER` | 128 | 每个 (userId, appType) 最大 GTID 数 |
| `MAX_BATCH_COUNTER_NUM` | 16 | 并发上下文同步操作上限 |

> `MAX_ACCESS_TYPES` 和 `MAX_APP_TYPES` 同为 64 并非巧合。每个 AccessType 对应一个 (AppType × 连接方式) 组合，即一个 Adapter。AccessType 总数受 64 上限约束——这是池化：最多 64 个 Adapter。AppType 是 Adapter 的属性之一，其数量自然不可能超过 Adapter 总数。极端情况下每种 AppType 只有一种连接方式，则 AppType 数 = AccessType 数 = 64；若有 AppType 有多种连接方式，AppType 就会少于 64。两者设相同值是对上限的统一池化管理。

### 2.2 Adapter

- [x] `userToConn_[MAX_USERS]` — userId（uid 高 8 位）→ 连接。adapter 内 AppType 恒定，用 userId 索引即可。
- [x] `connToUser_[MAX_CLIENTS_PER_ACCESS]` — 连接句柄 → userId。连接被动断开时反查 userId 以清理 `userToConn_`。

### 2.3 SessionMgr

- [x] `usernameToId_` — `username → userId`（uint8_t，0~63）。
- [x] `UserRecord[MAX_USERS][MAX_APP_TYPES]` — 64×64=4096 项二维表。每项：`char name[32]` + `static_vector<GTID, MAX_GTIDS_PER_USER>`。
- [x] `uidBitset` — bitset\\<MAX_USERS\\>。分配新 userId 时找空闲位。AppType 维度由请求自带，不需分配。
- [x] AppType 包含的 TaskType 集合 — `constexpr` 编译期常量。用于 `NewSessionReq` 闸门校验。

### 2.4 SessionData

- [x] `userAccessBitset[MAX_UID]` — 按 uid 直接索引，每位对应一个 AccessType。65536 × uint64_t = 512 KB。
- [x] `batchCounterResources_[MAX_BATCH_COUNTER_NUM]` — 每项 `{total, received}`。封装为类，`allocate()` 返回 token，批量发出的消息携带该 token，回复到达时凭 token 定位对应 counter。具体接口待定。

### 2.5 Gateway

- [x] `adapterTable_[MAX_ACCESS_TYPES]` — AccessType → EoAddress。adapter 启动时注册。

---

## Round 3 — SessionData 模式与职责

> 确认 SessionData 的两套工作模式及其触发条件、执行逻辑。

### 3.1 透传模式（Passthrough）

- [x] **触发**：收到带 GTID 的数据面请求（前端已指定 TaskType，GTID 编码了 TaskType）。
- [x] **动作**：`delegate(Router, msg)`，零拷贝转发。**不查 `userAccessBitset`，不做 fan-out**。
- [x] **AppType 角色**：无。GTID 已决定路由，不需要 AppType。

### 3.2 控制面交互（通用行为）

> 以下触发源对所有 AppType 统一，SessionData 响应 C 面通知维护 `userAccessBitset`。C→D 消息：`UserReset` / `UserLogin` / `UserLogout`。其中 `UserLogout` 需 D→C 回复（含活跃 adapter 数）。`UserDelete` 纯 C 面，D 面不感知——uid 删后无消息，复用前 `UserReset` 清空历史数据。

- [x] **UserLogin**
  - SessionMgr 发 `UserLogin{uid, appType, accessType, gtids}`。
  - SessionData 置 `userAccessBitset[uid]` 对应位。
  - 据 AppType 决定后续动作——不同 AppType 可能不同。例如 AiChat 类 AppType 若 gtids 非空，触发上下文同步：申请 BatchCounter → 组装批量消息（含所有 GTID）→ 发往 Router，由 Router 解包后逐 GTID `delegate` 给对应 Business EO。

- [x] **UserLogout**
  - SessionMgr 发 `UserLogout{uid, accessType}`。
  - SessionData 清 `userAccessBitset[uid]` 对应位。
  - 回复 SessionMgr（含该 uid 当前活跃 adapter 数，只有 Data 知道）。Mgr 据此决定后续（如活跃数为 0 时触发 context 持久化——未来功能，当前仅预留）。

- [x] **UserReset**
  - SessionMgr 分配新 userId 后发 `UserReset{uid}`。
  - SessionData 清空 `userAccessBitset[uid]` 全部位（安全清零，上次复用的历史数据）。
  - 无需回复。

### 3.3 数据面行为（AppType 特定）

> 以下行为仅与特定 AppType 相关，非通用。

- [x] **AiChatBus 上行消息 fan-out**
  - 收到 ACK 或 AI 回复 → 读 `uid`，查 `userAccessBitset[uid]` → 组装 `BatchFanOut{head, payload, targets}` → 发往 Gateway。
  - ACK 与回复走同一 fan-out 路径（具体格式见 Round 6）。

### 3.4 明确不做的事

- [x] ❌ 不分配 GTID（SessionMgr 的事）
- [x] ❌ 不维护 username↔userId 映射（SessionMgr 的事）
- [x] ❌ 不直接与 Adapter 通信（统一经 Gateway）
- [x] ❌ 不关心每个 adapter 内部有几个连接（Adapter 的事）
- [x] ❌ 不感知 context 持久化逻辑（SessionMgr 的事）

> **备忘**：旧方案中 SessionData 在收到数据面请求（如 AiChatReq）时会立即 fan-out 原始消息给其他 adapter——该方案已废弃，改为 AiChatBus 处理完毕后再上行广播 ACK。此条仅作笔记，最终整理时不列入正式决策。

---

## Round 4 — 消息下行路径

> 确认前端请求从 Adapter 到 Business EO 的完整路径及各节点行为。

### 4.1 全链路路径

```
前端 → Adapter → Gateway → SessionData → Router → Business EO → Service EO
```

### 4.2 各节点行为

- [x] **Adapter** `[AppType特定]`
  - 接收前端消息，填入消息头：`uid`（从自身 `connToUser_` 查）、`accessType`（编译期常量）、`appType`（编译期常量）、`sessionFlags`（编译期常量，含 `needsAck` 等）。
  - 消息头填入后发往 Gateway。

- [x] **Gateway** `[D面通用]`
  - 按消息类型 dispatch：控制类 → SessionMgr，数据类 → SessionData。
  - 消息原样转发，不读内容，不补字段。

- [x] **SessionData** `[D面通用]`
  - 收到带 GTID 的数据面请求 → `delegate(Router, msg)`，零拷贝转发。
  - **不查 `userAccessBitset`，不做 fan-out**。

- [x] **Router** `[D面通用]`
  - 按 GTID 提取 TaskType 位查表，`delegate()` 零拷贝转发给对应 Business EO。

- [x] **Business EO** `[D面通用]`
  - 收到 Adapter 原始组织的消息。只读 `sessionFlags` 决定行为（如 `needsAck` 则处理完毕后发 ACK）。AppType 相关的行为区分已在 Adapter 编译期编码进 `sessionFlags`，Business EO 不感知 AppType。

### 4.3 与旧方案对比

- [x] **旧方案**：Adapter 短路同步（同 adapter 内直接转发给其他 client）+ SessionData 跨 adapter fan-out 原始消息。
- [x] **新方案**：下行路径全链路仅转发，Adapter 和 SessionData 不做任何 fan-out。消息同步由 ACK 替代，ACK 的源头是 Business EO（AiChatBus 处理完毕后向上广播），而非 Adapter 或 SessionData。
- [x] **废弃原因**：消息未落地就同步存在不一致窗口（AiChatBus 可能因抢占式请求丢弃）。同步逻辑分散在 adapter 和 SessionData 两处。

---

## Round 5 — 消息上行路径

> 确认 ACK 和 AI 回复从 Business EO 广播回前端的完整路径。

### 5.1 上行路径

```
Business EO (AiChatBus) → SessionData → Gateway → Adapter → 前端
```

### 5.2 ACK 发送

- [x] **触发条件** `[D面通用]`：由 Adapter 编译期在 `sessionFlags.needsAck` 中决定。Business EO 处理完请求、写 context、分配 seq 后，仅当 `needsAck` 为 true 时发 `AiChatMsgAck{seq, content, …}`。
- [x] **ACK 内容**（ADR-0020）：`seq`（AiChatBus 分配的序列号）、`content`（原消息内容，同步给其他前端显示）。源端识别依赖 `head.accessType`（Gateway 不重写，保持原始值），各 Adapter 据此判断自己是源还是其他端。ACK 经 SessionData fan-out 到达所有有该 uid 连接的 adapter。
- [x] **SessionData 收到 ACK** `[D面通用]`：读 `uid`，查 `userAccessBitset[uid]` → 组装 `BatchFanOut{head, payload, targets}` → Gateway（格式见 Round 6）。

### 5.3 回复广播

- [x] **触发** `[D面通用]`：AiChatBus 收到 Service 回复后，向上发回应消息。
- [x] **SessionData 行为**：与 ACK 完全相同——读 `uid` → 查 `userAccessBitset[uid]` → `BatchFanOut` → Gateway。
- [x] **ACK 与回复共用 fan-out**：两者走同一条上行路径、同一套 `BatchFanOut/FanOutMsg` 格式。SessionData 不区分消息类型，统一处理。

### 5.4 Adapter 出向

- [x] **Gateway → Adapter**：`FanOutMsg{head, payload}`。
- [x] **Adapter** `[D面通用]`：收到后 `userToConn_[head.uid 的高 8 位 userId]` → 发送给前端。只有该 uid 的有源 client 会收到；其他 adapter 上该 uid 无连接则 `userToConn_` 为空，自然跳过。

---

## Round 6 — Fan-out 机制

> 确认上行广播的两级消息格式、各层职责、以及源 adapter 排除策略。
>
> **背景**：Round 5 确认上行路径为 `SessionData → Gateway → Adapter`。SessionData 组装 `BatchFanOut`（含 targets 位图），Gateway 拆解为逐 adapter 的 `FanOutMsg` 分发。
>
> **命名约定**：`targets` 为 `StaticBitMap<MAX_ACCESS_TYPES>` 或 `uint64_t`，每位对应一个 AccessType。`BatchFanOut` 是一次性发给 Gateway 的批量消息；`FanOutMsg` 是 Gateway 拆解后发给单个 Adapter 的消息。

### 6.1 BatchFanOut 格式

- [x] **6.1.1 字段定义**：`head`（含 `uid`、`gtidList`、`accessType`、`sessionFlags`）、`payload`（业务消息体，如 ACK 或 AI 回复）、`targets`（目标 AccessType 位图，直接取自 `userAccessBitset[uid]`）。

- [x] **6.1.2 `targets` 类型**：采纳方案 B — `uint64_t`。理由：(1) `MAX_ACCESS_TYPES` = 64 正好填满 64 位；(2) `userAccessBitset` 本身就是 `uint64_t`，直接拷贝零转换；(3) 将来若扩展 MAX_ACCESS_TYPES，随常量一并改为 `StaticBitMap` 或 `uint128_t` 即可。

- [x] **6.1.3 SessionData 不修改 `targets`**：`userAccessBitset[uid]` 原样拷贝为 `targets`，不做源排除。排除工作由 Adapter 自行完成——`head.accessType` 保持原始值不变（Gateway 不重写），Adapter 据此判断是给自己的 ACK 还是给其他端的消息同步。

- [x] **6.1.4 定义位置**：`common/message/`。SessionData→Gateway 的层间消息，与其他消息统一定义在 message 层。

- [x] **6.1.5 发送路径**：SessionData 收到上行消息（ACK / AI 回复）→ 读取 `uid` → `userAccessBitset[uid]` 作为 `targets` → 组装 `BatchFanOut` → 发往 Gateway。处理逻辑在 `DPlane/session/`。

- [x] **6.1.6 消费者**：Gateway 是 `BatchFanOut` 的唯一消费者，其他层不感知此消息类型。

### 6.2 FanOutMsg 格式

- [x] **6.2.1 字段定义**：`head`（含 `uid`、`gtidList`、`accessType`——保持原始值不变即源 Adapter 的 AccessType、`sessionFlags`）、`payload`（与 `BatchFanOut.payload` 相同，零拷贝或引用）。Gateway 只负责把 FanOutMsg 投递到正确目标（actor 消息系统层面），不修改 head 字段。

- [x] **6.2.2 不携带 `targets`**：已是单播消息，无需位图。

- [x] **6.2.3 定义位置**：`common/message/`。

- [x] **6.2.4 FanOutMsg 独立消息类型**：采纳方案 A — 独立类型。消息内部结构体可复用（head 字段通用），只需定义新消息类型标识。类型安全，Adapter 据此区分广播消息与普通下行响应。

### 6.3 各层职责

- [x] **6.3.1 SessionData 职责** `[D面通用]`：唯一职责——组装 `BatchFanOut{head, payload, targets}`，发往 Gateway。**不做**：targets 过滤、排除源 adapter、迭代分发。保持"只写位图、只读位图"的单一数据面角色。

- [x] **6.3.2 Gateway 职责** `[D面通用]`：收到 `BatchFanOut` → 遍历 `targets` 中每个置位的 AccessType → 查 `adapterTable_[accessType]` 获取 EoAddress → 构造 `FanOutMsg` → 逐 adapter 发送。

- [x] **6.3.3 Gateway 跳过未注册 adapter**：如 `adapterTable_[accessType]` 为空（该 AccessType 无 adapter 注册），跳过该项，不报错。

- [x] **6.3.4 Gateway 不做源排除**： 全部发送。排除是 Adapter 的业务逻辑，Gateway 只负责按 `targets` 逐 adapter 分发，不做消息内容层面的判断。

- [x] **6.3.5 Adapter 职责** `[D面通用]`：收到 `FanOutMsg` → 读 `head.uid` → `userToConn_[userId]` 查连接 → 有连接则发送给前端，无连接则丢弃。如是源 adapter（`head.accessType == myAccessType`），行为见 6.4。

### 6.4 源 Adapter 排除策略

> 核心问题：发起请求的 adapter（源）是否应收到自己请求的 ACK/回复广播？

- [x] **6.4.1 排除粒度**：采纳方案 C — **Adapter 自判**。SessionData 和 Gateway 均不做排除，所有 adapter 收到 `FanOutMsg`。源 adapter 据 `head.accessType == myAccessType` 识别自身；非源 adapter 正常展示。理由：(1) 源 adapter 需要 ACK 确认更新自身状态；(2) Gateway 不重写 head，`accessType` 即为源；(3) SessionData/Gateway 保持零决策。

- [x] **6.4.2 源 Adapter 收到自身 ACK 的行为**
  - 收到 `FanOutMsg`，Adapter 据 `head.accessType` 决定发给前端的消息类型：
    - `head.accessType == self` → 向本端前端发"消息送达"通知（不是原 ACK 透传）。
    - `head.accessType != self` → 向本端前端发"消息同步"通知（含其他端的请求内容）。
  - 是 Adapter 负责做这个区分，不是透传给前端自己判断。

### 6.5 边界情况

- [x] **6.5.1 targets 为空**：`userAccessBitset[uid]` 全零（用户仅在源 adapter 有连接，其他 adapter 均已断连）→ SessionData 不发送 `BatchFanOut`。至少打一条 log。

- [x] **6.5.2 Gateway 无可用 adapter**：所有 `targets` 对应的 `adapterTable_` 均为空 → Gateway 静默丢弃 `BatchFanOut`。至少打一条 log。

- [x] **6.5.3 目标 adapter 上 user 无连接**：FanOutMsg 到达 adapter，`userToConn_[userId]` 为空 → adapter 静默丢弃（用户在该 adapter 上未登录或已断连）。至少打一条 log。

### 6.n 本轮小结
- [x] 确认 Round 6 所有项无误

---

## Round 7 — C 面生命周期

> 确认注册、登录、登出、注销的完整流程与消息交互。
>
> **背景**：SessionMgr 是 C 面唯一权威——管理 username↔userId 映射、GTID 分配/回收、UserRecord。SessionData 响应 C 面通知维护 `userAccessBitset`。
>
> **命名约定**：D0~D7 为流程编号（对应原有设计文档中的流程步骤）。`RegisterReq/Rsp`、`LoginReq/Rsp` 等为消息类型名。

### 7.1 D0 注册 `[纯C面]`

> 路径：前端 → Adapter → Gateway → SessionMgr。D 面不参与。

- [x] **7.1.1 触发**：前端发 `RegisterReq{username}`。Adapter 编译期填入 `appType`（自身 AccessType 隐含），发往 Gateway → SessionMgr。

- [x] **7.1.2 SessionMgr 处理**
  - 检查 `username` 是否已在 `usernameToId_` 中注册（同一 username 同一 appType 不可重复注册）。
  - 从 `uidBitset` 中分配空闲 `userId`（uint8_t，0~63）。
  - 初始化 `UserRecord[userId][appType]`：填入 `name`，GTID 列表为空。
  - `usernameToId_[username] = userId`。
  - 返回 `RegisterRsp{userId}` 给前端。

- [x] **7.1.3 注册与登录分离**：注册只分配 userId，不建立连接状态。前端注册成功后需另行登录。

### 7.2 D1 首次登录 `[C面交互]`

> 路径：前端 → Adapter → Gateway → SessionMgr → (通知) → SessionData。

- [x] **7.2.1 触发**：前端发 `LoginReq{userId}`。Adapter 编译期填入 `appType` + `accessType`（均编译期常量，AccessType 隐含 AppType），发往 Gateway → SessionMgr。

- [x] **7.2.2 SessionMgr 处理**
  - 校验 `userId` 和 `appType` 合法（`UserRecord[userId][appType]` 存在）。
  - 从 `UserRecord` 取出该 (userId, appType) 的 GTID 列表。首次登录列表为空。
  - 向 SessionData 发 `UserLogin{uid, appType, accessType, gtids}`。`uid = (userId << 8) | appType`。
  - **C/D 边界原则**：SessionMgr 把自身已知的关于该 uid 的全部信息（appType、accessType、gtids）一并通知 SessionData。这些信息 SessionData 怎么用是 D 面的事——要不要触发上下文同步、怎么处理 GTID 列表——控制面只负责通知到位，不做 D 面决策。

- [x] **7.2.3 SessionData 处理**
  - 置 `userAccessBitset[uid]` 中 `accessType` 对应位。
  - 检查是否需要上下文同步：据 `appType` 判断（如 AiChat 类），且 `gtids` 非空。首次登录 `gtids` 为空 → **不触发**上下文同步。
  - 回复 SessionMgr：含该 `uid` 当前活跃 adapter 数。
  - **为何需要回复**：(1) 与登出对称——所有 C→D 通知统一有 D→C 回复；(2) 计数从 0→1 表示"冷启动"，SessionMgr 未来可据此触发从硬盘加载 context（与登出时计数归零触发持久化对称）；(3) 作为同步点——SessionMgr 确认 SessionData 处理成功后才回前端。

- [x] **7.2.4 响应前端**：SessionMgr 收到 SessionData 回复后，向 Adapter 返回 `LoginRsp{userId, gtids}`（gtids 为空列表）。Adapter 将 userId 记入 `userToConn_`。

### 7.3 D2 后续登录 `[C面交互]`

> 与 D1 流程相同，区别在于 GTID 列表非空，SessionData 据此触发上下文同步。

- [x] **7.3.1 触发与校验**：同 7.2.1、7.2.2。区别：`UserRecord` 中已有历史 GTID。

- [x] **7.3.2 GTID 列表天然合法**：`NewSessionReq` 建 task 时闸门已校验 AppType→TaskType 包含关系，通过的 GTID 写入 `UserRecord`。登录时直接取出，无需再过滤。

- [x] **7.3.3 上下文同步触发**
  - SessionData 判断 `appType` 为 AiChat 类且 `gtids` 非空 → 触发上下文同步。
  - 流程：从 `batchCounterResources_` 申请 BatchCounter（`allocate()` 返回 token）→ 组装批量消息（含所有 GTID，消息携带 token）→ 发往 Router。
  - Router 按 GTID 逐一解包，`delegate` 给对应 Business EO。
  - Business EO 各自处理（如 AiChatBus 查 context 返回历史消息），回复经 SessionData 直接发回源 adapter（SessionData 从 `UserLogin` 中已知 `accessType`），**不走 fan-out**。上下文同步数据量大且只有刚登录的前端需要，fan-out 到所有 adapter 是浪费带宽和算力。
  - BatchCounter 细节见 Round 9。

- [x] **7.3.4 响应前端**：`LoginRsp{userId, gtids}`，gtids 为非空列表。前端据此恢复会话列表。

### 7.4 D6 登出 `[C面交互]`

> 路径：前端 → Adapter → Gateway → SessionMgr → (通知) → SessionData → (回复) → SessionMgr。

- [x] **7.4.1 触发**：前端发 `LogoutReq{userId}`。Adapter 编译期填入 `appType` + `accessType`。Adapter **暂不**清除 `userToConn_[userId]`——等回路确认回来再清。

- [x] **7.4.2 SessionMgr → SessionData**：发 `UserLogout{uid, accessType}`。

- [x] **7.4.3 SessionData 处理**
  - 清 `userAccessBitset[uid]` 中 `accessType` 对应位。
  - 统计该 `uid` 当前剩余活跃 adapter 数（`userAccessBitset[uid]` 中置位数）。
  - 回复 SessionMgr：含活跃 adapter 数。

- [x] **7.4.4 SessionMgr 收尾**：据活跃 adapter 数决定后续（如为 0 则未来可触发 context 持久化——当前预留）。返回 `LogoutRsp` 给前端。Adapter 收到 `LogoutRsp` 后清除 `userToConn_[userId]`——形成完整回路。

### 7.5 D7 注销 `[纯C面]`

> 路径：前端 → Adapter → Gateway → SessionMgr。D 面不感知。

- [x] **7.5.1 触发**：前端发 `DeleteReq{userId}`。Adapter 编译期填入 `appType`。

- [x] **7.5.2 SessionMgr 处理**
  - 遍历 `UserRecord[userId][appType]` 中所有 GTID，逐一 `pool_.deallocate()` 回收。
  - 清空 `UserRecord[userId][appType]`。
  - 从 `usernameToId_` 中移除该 username。
  - 释放 `userId`（清 `uidBitset` 对应位）。
  - 返回 `DeleteRsp` 给前端。

- [x] **7.5.3 不通知 D 面**：uid 已删除，后续无消息到达。`userId` 被复用时，SessionMgr 先发 `UserReset{uid}` 给 SessionData 清零历史 bitset（见 Round 3.2）。

### 7.n 本轮小结
- [x] 确认 Round 7 所有项无误

---

## Round 8 — 数据面生命周期

> 确认新建/删除会话、client 断连、消息同步的完整流程。
>
> **背景**：SessionMgr 是 GTID 分配/回收的唯一权威。新建/删除会话需通知其他端同步会话列表。消息同步复用 Round 5/6 的 ACK fan-out 机制。

### 8.1 D3 新建会话 `[C面交互 + AppType特定]`

> **核心原则**：不存在独立的"建空 session"控制请求。新建会话总是伴随第一条数据消息一起到达（捆绑请求）。控制面处理后转数据面执行，数据面的 ACK fan-out 自然充当"新会话通知"——其他端看到陌生 GTID 即知有新会话。

> 路径：前端 → Adapter → Gateway → SessionMgr（控制）→ SessionData → Router → BusinessEO（数据）→ ACK fan-out → 各 Adapter。

- [x] **8.1.1 捆绑请求**：前端发第一条消息时，消息中携带 `taskType`（新建哪种会话）及数据内容。Adapter 编译期填入 `appType` + `accessType`。不存在独立的 `NewSessionReq`。

- [x] **8.1.2 协议约定——特殊 GTID 标识新 Task**：前端发捆绑请求时填入一个特殊 GTID 值——`taskType` 位如实填写（指示新建哪种会话），序列号位填全 1（`0xFFFF` 或等价哨兵值）。Gateway 据此将消息路由到 SessionMgr 而非 SessionData。SessionMgr 分配正式 GTID 后替换该哨兵值。这涉及 GTID 编码定义的更新（见 9.5.2）。

- [x] **8.1.3 SessionMgr 控制面处理**
  - 闸门校验：该 `appType` 包含的 TaskType 集合是否包含 `taskType`。不通过则拒绝。
  - 分配 GTID，写入 `UserRecord[userId][appType]`。
  - 将控制面处理结果（GTID）填入消息，**连同原始数据**一并转发给 SessionData。

- [x] **8.1.4 数据面执行**：SessionData 收到后按正常数据路径处理——`delegate(Router, msg)` → Router 按 TaskType 路由 → BusinessEO 执行业务逻辑。

- [x] **8.1.5 ACK fan-out 即通知**：BusinessEO 处理后发 ACK → SessionData → `BatchFanOut` → 各 Adapter。其他端收到含陌生 GTID 的 ACK，自然知道新会话已创建，无需单独的"会话创建通知"fan-out。源端收到 ACK 即确认 session 创建成功 + 消息已处理。

- [x] **8.1.6 将来若有纯控制请求**：若日后出现不携带数据的独立控制请求（如单独建 session），则走三角形路径：Mgr 处理 → 结果给 Data → Data fan-out/回复 → 回 Adapter。当前不做此设计。

### 8.2 D4 删除会话 `[C面交互 + AppType特定]`

> 删除是纯控制请求（不捆绑数据）。走三角形路径：Mgr 处理 → Data fan-out 通知 + 确认 → 回 Adapter。SessionMgr 不直接回 Adapter，因为通知其他端是 D 面的职责。

> 路径：前端 → Adapter → Gateway → SessionMgr → SessionData → (fan-out) → 各 Adapter（含源端）。

- [x] **8.2.1 触发**：前端发 `DeleteSessionReq{userId, gtid}`。Adapter 填入 `appType` + `accessType`。

- [x] **8.2.2 SessionMgr 校验与回收**：校验 `gtid` 属于该 (userId, appType)。从 GTID 池回收，从 `UserRecord` 中移除。将处理结果（含 `uid`、`gtid`）发给 SessionData。

- [x] **8.2.3 SessionData fan-out**：收到 SessionMgr 通知后，组装 `BatchFanOut`（携带 `SessionDeleted` 语义）→ Gateway → 各 Adapter。**源 adapter 也在 targets 中**——源端收到即确认删除成功；其他端收到后从会话列表中移除该会话。

- [x] **8.2.4 三角形路径说明**：不走 SessionMgr 直接回 Adapter 的原路返回，而是绕经 SessionData。因为通知其他端、向源端确认都是 D 面职责，SessionMgr 只做控制面裁判（校验 + 回收），不管通知。

### 8.3 D5 client 断连 `[C面交互]`

> 路径：Adapter 检测断连 → 自动触发登出 → 同 D6 流程（见 7.4）。

- [x] **8.3.1 检测**：Adapter 检测到 TCP/WS 连接断开。

- [x] **8.3.2 先走断连流程，后清本地状态**：Adapter 从 `connToUser_` 反查 `userId`，但**暂不**清除 `userToConn_[userId]` 和 `connToUser_[conn]`。先向 FlowHub 内部发起自动登出流程（8.3.3），收到回路确认后再清理本地状态——形成完整回路。

- [x] **8.3.3 自动登出流程**：Adapter 向 SessionMgr 发 `LogoutReq{userId}`（Adapter 填入 appType + accessType）。后续流程同 7.4：SessionMgr → `UserLogout` → SessionData 清位 → 回复活跃 adapter 数。SessionData 的 fan-out 回复到达本 Adapter 后，Adapter 再清除 `userToConn_[userId]` 和 `connToUser_[conn]`。

- [x] **8.3.4 与手动登出的区别**：仅触发源不同（Adapter 检测断连 vs 前端主动发），SessionMgr/SessionData 处理完全一致。Adapter 侧行为也统一——都是先走完内部流程、收到回路确认后再清除 `userToConn_`。

### 8.4 消息同步 `[AppType特定]`

> ACK fan-out 和 AI 回复广播已在 Round 5/6 完整定义，此处仅做汇总引用。

- [x] **8.4.1 ACK fan-out**：Business EO（AiChatBus）处理请求后发 ACK（含 seq、content）→ SessionData → `BatchFanOut` → Gateway → 各 Adapter。Adapter 据 `head.accessType` 发"送达"或"同步"给前端。详见 Round 5.2、Round 6。

- [x] **8.4.2 AI 回复广播**：AiChatBus 收到 Service 回复后，走与 ACK 完全相同的上行 fan-out 路径。详见 Round 5.3。

- [x] **8.4.3 上下文同步回源**：登录后上下文同步的回复**不走 fan-out**，直接回源 adapter。详见 Round 7.3.3。

### 8.n 本轮小结
- [x] 确认 Round 8 所有项无误

---

## Round 9 — 跨切面与常量定稿

> 最后一轮，收尾所有跨切面细节：标志位、超时、错误处理、常量、消息头。
>
> **背景**：前 8 轮已确认概念、数据结构、消息路径、生命周期。本轮不引入新流程，只定稿散落各处的常量与约定。

### 9.1 sessionFlags 设计 `[AppType特定]`

> 设计为 `SessionFlags` 值类型类（非模板），消息中直接作为字段使用。标志位由 Adapter 编译期通过 `AppType` 决定，BusinessEO 运行时读取。消灭裸 `uint8_t` 位运算的魔法数字。

#### 类设计

- [x] **9.1.1 `BitFlags` 枚举**：定义在 `SessionFlags` 内部，每 bit 一个值，幂次递增。`enum class BitFlags : uint8_t { needAckBit = 0x01 };`

- [x] **9.1.2 默认构造函数**：`constexpr SessionFlags() : flags_(0) {}`。用于构造空消息，字段后续逐个填入。

- [x] **9.1.3 静态工厂 `make<>()`**：`template <AppType AT> static constexpr SessionFlags make()`。内部 `switch (AT)` 做 `AppType → flags 值` 映射。编译期完成，零运行时开销。新增 AppType 只需在 switch 中加 `case`，老代码不碰。

- [x] **9.1.4 私有值构造函数**：`explicit constexpr SessionFlags(uint8_t v)`，仅 `make()` 内部调用。禁止外部用裸 `uint8_t` 构造，防止魔法数字绕过。

- [x] **9.1.5 查询接口**：`constexpr bool isNeedAck() const { return flags_ & static_cast<uint8_t>(BitFlags::needAckBit); }`。BusinessEO 直接调用，编译为一条 `test` 指令。后续新增 flag 仿此模式加新方法。

- [x] **9.1.6 用法示例**

  ```cpp
  // 每个 Adapter 编译期定义自身 AppType
  static constexpr AppType MyAppType = AppType::AiChat;

  // Adapter 构造消息（通用写法，不硬编码 AppType 值）
  RequestHead head;
  head.sessionFlags = SessionFlags::make<MyAppType>();

  // BusinessEO 读取
  if (msg.head.sessionFlags.isNeedAck()) { sendAck(); }
  ```

- [x] **9.1.7 参考实现**

  ```cpp
  // common/SessionFlags.hpp
  #pragma once

  #include <cstdint>
  #include "common/type/AppType.hpp"

  class SessionFlags {
  public:
      enum class BitFlags : uint8_t {
          needAckBit = 0x01,
      };

      // 默认构造：用于构造空消息，flags_ = 0
      constexpr SessionFlags() : flags_(0) {}

      // 静态工厂：模板参数为 AppType，编译期完成映射
      template <AppType AT>
      static constexpr SessionFlags make() {
          uint8_t v = 0;
          switch (AT) {
              case AppType::AiChat:
                  v = static_cast<uint8_t>(BitFlags::needAckBit);
                  break;
              default:
                  break;
          }
          return SessionFlags{v};
      }

      // 查询接口：运行时一条 test 指令
      constexpr bool isNeedAck() const {
          return flags_ & static_cast<uint8_t>(BitFlags::needAckBit);
      }

  private:
      uint8_t flags_;
      // 私有值构造函数：仅供 make() 内部调用，禁止外部裸 uint8_t 构造
      explicit constexpr SessionFlags(uint8_t v) : flags_(v) {}
  };
  ```

  Adapter 编译期路径：`SessionFlags::make<AppType::AiChat>()` → 常量折叠，零指令。
  BusinessEO 运行时路径：`msg.head.sessionFlags.isNeedAck()` → `test al, 1`，单周期。

- [x] **9.1.8 位置**：`common/SessionFlags.hpp`。

- [x] **9.1.9 当前映射**

  | AppType | needAckBit |
  |---------|-----------|
  | AiChat | 1 |
  | 其他 | 0 |

- [x] **9.1.10 gen_code.py 适配**：`.mt` 文件中增加 `include "common/SessionFlags.hpp"` 语法，生成脚本能识别并生成对应的 `#include "common/SessionFlags.hpp"`。文件不存在时需报错，不能静默跳过。

### 9.2 BatchCounter 策略 `[D面通用]`

> BatchCounter 用于上下文同步：`allocate()` 返回 token，批量发出 N 条请求，每收到一条回复 `count++`，`count == total` 时完成。计数完全由消息驱动——收到一条回复才加一，无主动轮询或定时器。

- [x] **9.2.1 超时检测——前端负责**：FlowHub 当前无定时器机制，无法主动检测超时。上下文同步未在预期时间内收齐的判定由前端维护自己的超时计时。即使日后 FlowHub 增加了超时能力，前端自行维护也合理。

- [x] **9.2.2 资源防枯竭——分配时检查**：`allocate()` 申请新 counter 时，遍历当前所有已占用的 counter，若某 counter 的 `allocate()` 时刻距今超过合理阈值（建议 5 秒），视为已废弃，强制释放资源。以此防止异常情况下的资源泄漏。

- [x] **9.2.3 消息等待超时——日后迭代**：基于定时器的主动超时丢弃机制不在当前实现范围，列为后续迭代计划。

- [x] **9.2.4 接口**：`BatchCounter` 封装为类，`allocate(N) → index`（即 `batchCounterResources_` 的下标），`onReply(index)` 将对应资源计数 -1，`isComplete(index) → bool` 判断计数是否归零，`isTimeout(index) → bool` 供分配时 GC 检查。不需要单独的 token 类型，index 本身就是标识。

- [x] **9.2.5 消息携带 index**：SessionData 发出批量上下文同步请求时，每条消息携带 `batchIndex`。BusinessEO 处理后回复原样带回 `batchIndex`。SessionData 收到回复后凭 `batchIndex` 定位 `batchCounterResources_[batchIndex]`，调用 `onReply(batchIndex)` 计数 -1。

### 9.3 错误处理基线 `[通用]`

> 定义各层通用错误码与错误传播路径。详细错误码由各层在实现时扩展。

- [x] **9.3.1 错误传播路径**：错误沿请求的反向路径返回——BusinessEO → Router → SessionData → Gateway → Adapter → 前端。每层可追加本层错误信息。消息中携带 `errorCode` 字段承载错误码。

- [x] **9.3.2 通用错误码**
  - `INVALID_GTID`：GTID 不存在或不属于该 user（Router 返回）。
  - `TASK_TYPE_NOT_ALLOWED`：闸门校验不通过（SessionMgr 返回）。
  - `NO_GTID_NEW_TASK`：前端未声明新 Task 又无 GTID（Router 返回，见 8.1.2）。
  - `TIMEOUT`：下游处理超时。
  - `INTERNAL_ERROR`：内部未知错误。

- [x] **9.3.3 错误与 ACK 的关系**：若 `needsAck` 为 true 但处理失败，BusinessEO 不发 ACK（ACK 仅表示成功处理）。错误单独经错误路径返回。

- [x] **9.3.4 错误码定义**：`enum class ErrorCode : uint8_t`，定义在 `common/type/`，与 `AccessType`、`AppType` 同级。当前处理方式：打一条错误 log + 将错误码填入消息返回给前端。详细错误处理策略日后迭代。

### 9.4 常量最终汇总 `[通用]`

> 以下常量已在 Round 2 确认，本轮仅为最终定稿。

- [x] **9.4.1 常量表**

| 常量 | 值 | 说明 |
|------|----|------|
| `MAX_USERS` | 64 | userId 上限 |
| `MAX_APP_TYPES` | 64 | AppType 枚举值上限 |
| `MAX_ACCESS_TYPES` | 64 | Adapter 总数上限（池） |
| `MAX_UID` | 65536 | = MAX_USERS × MAX_APP_TYPES |
| `MAX_CLIENTS_PER_ACCESS` | 64 | 每 Adapter 最大连接数 |
| `MAX_GTIDS_PER_USER` | 128 | 每 (userId, appType) 最大 GTID 数 |
| `MAX_BATCH_COUNTER_NUM` | 16 | 并发上下文同步操作上限 |

- [x] **9.4.2 常量定义方式**：常量以 `.mt` 文件定义，由 `gen_code.py` 生成对应的 `Constants.hpp`。与其他类型（`AccessType`、`AppType` 等）保持一致——定义与生成分离，脚本统一管理。

### 9.5 消息头字段定稿 `[通用]`

> 汇总所有内部消息必带字段，明确由谁填入、谁读取。

- [x] **9.5.1 字段汇总**

| 字段 | 类型 | 填入者 | 读取者 | 说明 |
|------|------|--------|--------|------|
| `uid` | uint16_t | Adapter（运行时 `userToConn_` 查 userId + 编译期 appType 拼装） | SessionMgr, SessionData, Gateway, Adapter | 全链路携带 |
| `gtidList` | GTID 列表 | 前端（或 SessionMgr 分配后填入） | Router, BusinessEO, SessionData | 序列号全 1 = 新 Task 请求（见 8.1.2） |
| `accessType` | AccessType | Adapter（编译期常量） | Gateway, SessionMgr, SessionData, Adapter | Adapter 回程路由 + fan-out 位图索引 |
| `appType` | AppType | Adapter（编译期常量，AccessType 隐含） | SessionMgr（闸门校验 + UserRecord 索引）, SessionData（上下文同步判断） | 控制面请求携带 |
| `sessionFlags` | `SessionFlags` | Adapter（`make<AppType>()` 编译期构造） | BusinessEO（`isNeedAck()` 等查询） | 见 9.1 |
- [x] **9.5.2 GTID 序列号全 1 = 新 Task 请求**：前端发捆绑请求时填入特殊 GTID——`taskType` 位如实填写，序列号位填全 1（哨兵值）。Gateway 据此路由到 SessionMgr。SessionMgr 分配正式 GTID 后替换。此为 GTID 编码层面的约定，需要更新 GTID 定义。


### 9.n 本轮小结
- [x] 确认 Round 9 所有项无误

---

---

## 执行状态

| Round | 状态 |
|-------|------|
| Round 1  基础约束 | ✅ 已确认 |
| Round 2  数据结构 | ✅ 已确认 |
| Round 3  SessionData模式 | ✅ 已确认 |
| Round 4  消息下行 | ✅ 已确认 |
| Round 5  消息上行 | ✅ 已确认 |
| Round 6  Fan-out | ✅ 已确认 |
| Round 7  C 面生命周期 | ✅ 已确认 |
| Round 8  D 面生命周期 | ✅ 已确认 |
| Round 9  跨切面 | ✅ 已确认 |

---

## ⚠️ 后续 TODO

- [ ] 废弃 `sourceAddress`：同步修改 Business/Service 层代码（本次不做）
- [ ] Adapter 基类设计（WebBase 等共享代码抽取）

---

## 附录：方案取舍记录（ADR 素材）

> 记录讨论过程中所有被考虑、被否决、被采纳的方案及完整推理链。供日后整理为正式 ADR。

---

### A. 三层类型体系的建立

#### A-1. WindowId 的引入与废弃

| 阶段 | 内容 |
|------|------|
| **原始动机** | 前端需要区分多个窗口/标签页，需要一个标识 |
| **第一版方案** | 引入 WindowId，后端维护窗口计数和生命周期 |
| **问题** | WindowId 承载了三种信息：接入方式（哪个 adapter）、窗口计数（多少个窗口）、GTID 关联（哪个窗口对应哪个 GTID）。三件事混在一个字段里，语义混沌 |
| **否决过程** | 接入方式 → AccessType；窗口身份 → GTID + clientId（后来 clientId 也被毙掉）；窗口计数 → adapter 内部自行维护 |
| **最终** | WindowId 概念从设计中完全移除 |

#### A-2. 为什么需要第三种 Type

| 阶段 | 内容 |
|------|------|
| **原始状态** | 只有 AccessType（CLI/Web/Desktop）和 TaskType（GTID 编码）。设计假设所有连接到同一 adapter 的 client 行为一致 |
| **发现缺口** | 同一 adapter（如 Mobile App adapter）可能连接两种截然不同的前端：AI 对话 App 和智能家居 App。它们对上下文同步、ACK、消息广播的需求完全不同 |
| **关键洞察** | 一个 App 就是一个 TaskType 的集合。"消息同步"不是 AiChat 这个 Task 自带的需求，而是"支持同用户多端接入的 App"才有的需求。CLI AiChat 接入时，根本没有消息同步和 ACK 的必要——"他妈的终端接入哪有什么消息同步" |
| **最终** | 引入 AppType，作为 AccessType（怎么连）和 TaskType（哪个任务）之间的中间抽象——"做什么" |

#### A-3. AppType 可见范围的三次收窄

| 版本 | 可见范围 | 说明 |
|------|----------|------|
| V1 | 前端 ↔ Adapter ↔ SessionData | SessionData 维护行为表，根据 AppType 做决策（上下文同步、ACK 等） |
| V2 | 前端 ↔ Adapter ↔ SessionMgr | 行为决策上移至 SessionMgr。SessionData 只执行指令 |
| V3 | 前端 ↔ Adapter ↔ SessionMgr | SessionData 完全不感知 AppType。Adapter 通过 `sessionFlags` 编码行为意图；SessionMgr 做 GTID 过滤和校验；SessionData 只按位图 fan-out，零决策 |

**收窄的理由链**：
1. userId 按 AppType 隔离后，fan-out 天然正确——不同 AppType 的 userId 一定落在不同的 adapter 集合，SessionData 无需过滤
2. ACK 是否发送应该由 Adapter 决定——Adapter 编译期就知道自己是什么 AppType，直接设 `sessionFlags.needsAck`
3. 上下文同步是否触发应该由 SessionData 自己判断——收到 `UserLogin` 里的 GTID 列表，有 GTID 就同步，没有就跳过，不需要查表
4. 三条加起来：SessionData 不需要 AppType 做任何决策

#### A-4. SessionType 更名为 AppType

| 方案 | 评价 |
|------|------|
| SessionType | 与架构层名 Session（SessionMgr/SessionData）重名，易混淆；暗示其作用域在 Session 层，实际止于 SessionMgr |
| **AppType** ✅ | 语义直白——前端 App 的类型；与 AccessType/TaskType 并列对齐；文档和代码中一目了然 |

---

### B. 复杂度消除链

#### B-1. Adapter 粒度：按连接方式分 → 按 (AppType × 连接方式) 分

| 阶段 | 内容 |
|------|------|
| **初始方案** | 一个 WebAdapter 同时服务 AiChat Web 客户端和 SmartHome Web 客户端。Adapter 内部需要 `clientSessionType_[clientId]` 表，fan-out 时逐 client 过滤 |
| **问题** | Adapter 承担了 AppType 感知和过滤职责，复杂度内聚在 adapter |
| **关键讨论** | "我这个算不算过度设计？假如我 accessAdapter 根本就不是一个连接方式一个，而是一个 AppType 的一个连接方式一个呢？" |
| **最终** | 每种 (AppType × 连接方式) 组合一个独立 Adapter。每个 Adapter 内所有 client 天然同质，fan-out 全量发送无需过滤。同类连接方式的 Adapter 通过基类/组合共享底层代码（如 WebBase）。`clientSessionType_` 表直接砍掉 |
| **AccessType 语义同步更新** | AccessType 从"纯连接方式"变为 Adapter 唯一标识，枚举值 = (AppType, 连接方式) 笛卡尔积：`AiChatWeb`、`AiChatCLI`、`SmartHomeWeb`、`SmartHomeApp` 等 |

#### B-2. Adapter 内 user 多开 → 单连接

| 阶段 | 内容 |
|------|------|
| **初始方案** | 同一 Adapter 允许同一 user 多个连接。需要 `userToClients_[userId]`（bitset）+ 遍历发送 + clientId 管理整套机制 |
| **分析** | CLI 不可能多开；Web 标签页在前端自己管理多视图即可；App 单实例。实际场景中同一设备同一 App 无多开需求 |
| **最终** | 一 user 一连接。`userToConn_[userId]` O(1) 直查。砍掉 `userToClients_` bitset 和 clientId |

#### B-3. clientId 的逐步淘汰

| 阶段 | 内容 |
|------|------|
| **原始用途** | Adapter 分配 clientId 标识连接，用于：① 注册/登录响应匹配回前端连接；② fan-out 时排除源 client |
| **第一次简化** | 规定单连接后，userId 即可唯一标识连接 |
| **第二次简化** | 注册/登录响应走 `accessType`（Gateway 查 `adapterTable_` 路由回对应 adapter），adapter 内用 userId 定位连接 |
| **fan-out 排除** | Adapter 用临时变量跳过源连接即可，不需要 clientId 写入消息 |
| **最终** | clientId 全线移除。Adapter 内只用 userId 查连接 |

#### B-4. userId 跨 AppType 共享 → 按 AppType 隔离

| 阶段 | 内容 |
|------|------|
| **初始方案** | 同一真实用户在不同 AppType 前端登录共用同一 userId。SessionData fan-out 时需查 AppType 过滤，否则 SmartHome Adapter 会收到 AiChat 消息 |
| **问题分析** | 引入了 GTID 粒度 fan-out 表（`gtidAccessBitset[65536] = 512KB`）的讨论，以及 SessionData 过滤逻辑，复杂度急剧膨胀 |
| **关键洞察** | 不同的 App 就是不同的服务。账号不打通是合理的——AiChat 的上下文和 SmartHome 的设备状态没有共享数据 |
| **最终** | `usernameToId_` key = `username + "_" + to_string(appType)`。同一 username 在不同 AppType 下分配不同 userId。SessionData 的 `userAccessBitset` 按 userId 索引自动隔离，fan-out 零过滤 |
| **副作用简化** | `clientSessionType_` 不需要；SessionData `accessTypeToSessionType_` 常量表不需要；fan-out 过滤逻辑不需要。一条约束消掉了三张表和两处逻辑 |

---

### C. 消息同步机制的演进

#### C-1. 旧方案：adapter 短路 + SessionData 跨 adapter fan-out

| 阶段 | 内容 |
|------|------|
| **原始设计** | 请求发出时：同 adapter 内短路同步（adapter 直接转发给同 user 其他 client）+ 跨 adapter 由 SessionData fan-out 原始消息。AI 回复走另一条独立 fan-out 路径 |
| **问题** | ① 消息尚未被 AiChatBus 处理就同步出去了——如果 AiChatBus 因抢占式请求（ADR-0020）丢弃该消息，已经同步的 client 会看到不存在的消息；② 同步逻辑分散在 adapter 和 SessionData 两处，两套路径 |

#### C-2. 新方案：AiChatBus ACK 上行广播

| 阶段 | 内容 |
|------|------|
| **新设计** | 请求一路下到 AiChatBus → 分配 seq → 写 context → 发 Service 请求 → **然后**向上广播 ACK。ACK 和 AI 回复走同一条上行 fan-out 路径 |
| **关键优势** | ① AiChatBus 是唯一排序权威（seq 分配者），ACK 在消息落地后发出，不存在不一致窗口；② 所有上行广播（ACK + 回复）走同一套 BatchFanOut/FanOutMsg 机制 |
| **与 ADR-0020 对齐** | ADR-0020 已定义 `AiChatMsgAck{seq, content, …}` 消息类型，新方案直接复用 |

#### C-3. ACK 不是 TaskType 的事，是 AppType 的事

| 阶段 | 内容 |
|------|------|
| **初始假设** | AiChatBus 无条件发送 ACK |
| **问题** | CLI AiChat 是单端终端接入，根本不需要 ACK。ACK 是否发送不应由 Business 层的 AiChatBus 决定 |
| **解决方案** | Adapter 编译期知道自身 AppType，在发出的请求中设置 `sessionFlags.needsAck`。AiChatBus 只在 flag 为 true 时发 ACK。CLI Adapter 设 false，Web Adapter 设 true |
| **原则** | Business 层（TaskType 级）不应该决定 Session 层（AppType 级）的行为 |

---

### D. C 面与 D 面的职责边界

#### D-1. 标准通知模式

| 阶段 | 内容 |
|------|------|
| **讨论** | SessionMgr（C 面）登录后是否要替 SessionData（D 面）决定"要不要做上下文同步"？ |
| **结论** | C 面动作应保持通用精炼——登录后发标准通知 `UserLogin{userId, appType, accessType, gtids}` 给 SessionData。SessionData 自行决定后续行为 |
| **理由** | ① "通知登录"是所有 AppType 的通用动作；② GTID 列表是登录事件的自然信息负载（SessionMgr 天然持有），不是额外动作；③ 如果 SessionMgr 决定同步，那是 C 面替 D 面做了业务决策——违反了 C/D 分离原则 |
| **反例验证** | 如果 SessionMgr *不*发 GTID 列表，而是让 SessionData 自己来查——那才是为某个业务增加了 C 面 API，增加了路径 |

#### D-2. GTID-userId 绑定归属 SessionMgr

| 理由 | 说明 |
|------|------|
| 分配时自然绑定 | GTID 和 userId 都是 SessionMgr 分配的，绑定在一起最自然 |
| GTID 写入 Context | GTID 所属 userId 可直接写入 Context，不需要额外的反向查询结构 |
| 注销时统一回收 | 删号时 SessionMgr 遍历该 userId 的所有 GTID 逐个 `pool_.deallocate()`，这是 C 面职责 |
| 登录通知需要 | SessionMgr 持有 userId→GTIDs 映射，登录时直接取出随通知发给 SessionData |
| **如果放在 SessionData** | SessionData 需要维护 userId→GTIDs 映射表，且注销时 SessionMgr 需要跨层查询——引入了不必要的耦合 |

---

### E. 表结构简化链

#### E-1. Adapter 表结构演化

```
V1（多连接 + 多 SessionType）：
  clientToConn_[MAX_CLIENTS]
  clientToUser_[MAX_CLIENTS]
  userToClients_[MAX_USERS]        ← bitset，fan-out 遍历
  clientSessionType_[MAX_CLIENTS]  ← fan-out 时逐一比对过滤

V2（单连接 + 单一 AppType per adapter）：
  userToConn_[MAX_USERS]           ← O(1) 直查
  connToUser_[MAX_CLIENTS]        ← 断连时反查 userId

砍掉：clientToConn_、clientToUser_、userToClients_、clientSessionType_
```

#### E-2. SessionData 表结构演化

```
V1（感知 AppType）：
  userAccessBitset[MAX_USERS]
  batchCounterResources_[MAX_BATCH]  ← 已有
  AppType 行为表                     ← 查表决策
  AppType → TaskType 映射表          ← fan-out 过滤
  accessTypeToSessionType_ 常量表    ← target 过滤

V2（不感知 AppType）：
  userAccessBitset[MAX_USERS]        ← userId 隔离后自动正确
  batchCounterResources_[MAX_BATCH]  ← 不变

砍掉：行为表、映射表、常量表。SessionData 回归纯执行。
```

#### E-3. userId 编码方案

| 阶段 | 内容 |
|------|------|
| **原始方案 V1** | userId = `uint8_t`，全局 0~63。`usernameToId_` key = `username + "_" + appType` 做字符串拼接实现 AppType 隔离 |
| **问题** | 字符串拼接不优雅；userId 自身不携带 AppType 信息，SessionData 若需要 AppType 需从消息头取或维护本地表 |
| **最终方案** ✅ | uid = uint16_t，编码 [userId:8][AppType:8]。uid 自携带 AppType（uid & 0xFF 即得），无需字符串拼接，无需本地表 |
| **UserRecord** | `array[64][64]` = 4096 项，每项 `name[32] + static_vector<GTID, 128>`。最坏 ~2MB，实际大部分格子为空（用户不会在所有 AppType 注册） |
| **常量** | MAX_USERS = 64（userId 上限），MAX_APP_TYPES = 64，MAX_GTIDS_PER_USER = 128 |

#### E-4. userAccessBitset 索引方案

| 方案 | 大小 | 索引方式 |
|------|------|----------|
| **A：flat** ✅ | 65536 × 8B = **512 KB** | `bitset[userId]`，零翻译 |
| B：2D | 64 × 64 × 8B = **32 KB** | bitset[userId][appType]，需拆 uid |

**选择 A 的理由**：
- 512 KB 可接受。D 面 fan-out 比 C 面操作更频繁，省一步拆解换零翻译更值
- AccessType 本身有 AppType 属性，userId 也自携带 AppType，理论上每个 userId 只有其 AppType 对应的少数 AccessType 可能置位，可进一步省内存。但省不了多少（32KB→也许十几KB），多一步翻译，买卖不值
- 64-bit bitset 中寻 1 用 `__builtin_ctzll` 等内置函数，稀疏位不造成性能问题

---

### F. 关键设计原则总结

1. **C 面动作通用精炼，D 面自行决策** — C 面只发标准通知，不替 D 面做业务判断
2. **谁分配谁持有** — GTID 和 userId 都是 SessionMgr 分配，绑定关系自然归 SessionMgr
3. **UserId 隔离消解过滤复杂度** — 不同 AppType 不同 userId → fan-out 天然正确，无需过滤
4. **编译期常量化运行时决策** — AppType 包含的 TaskType 集合、sessionFlags 设置都在编译期确定，运行时零开销
5. **消息落地后才同步** — ACK 在 AiChatBus 写 context 后发出，消除不一致窗口
6. **Business 层不感知 AppType** — TaskType 是 Business 层的唯一概念，AppType 止于 SessionData。业务消息全链路透传，ACK 等标志由 Adapter 编译期填入
7. **Adapter 功能单一化** — 每种 (AppType × 连接方式) 独立 Adapter，内部零过滤

