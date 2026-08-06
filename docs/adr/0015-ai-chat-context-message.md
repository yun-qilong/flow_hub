# ADR-0015：单 AI 对话——Context 与消息体设计

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳（部分修订） | 2026-06-16 | 韵启龙 |

> **修订**：
> - `AiChatStage` 枚举已被 [ADR-0020](../adr/0020-seq-version-control.md) 废弃，改为 `pendingReqSeq` 版本号机制
> - `turnCount` 由 `messageCount` 取代（ADR-0020）
> - `businessReplyAddr` 引用的 `sourceAddress` 字段已被 [ADR-0024](../adr/0024-head-accesstype-reuse.md) 移除
> - 对话历史存储机制（messagesBuffer）和消息体传递 JSON 字符串的决策仍然有效
>
> **修订（2026-07-30）**：
> - **架构变化**：本 ADR 最初的决策背景是 AiChatBus 作为单 AI 对话的唯一执行者，自行管理完整生命周期——接收用户消息、维护对话历史、调用 AI API、回复结果。FT0002-B 引入 Session 层编排器（AiAgora）后，架构职责重新划分：Session 层负责一个话题的全局编排（上下文维护、轮次控制、多 AI 协调），Bus 层退化为子任务执行器——接收编排器组装好的 messages JSON，调用 API，返回结果，不维护历史。
> - **决策逻辑**：职责上移自然导致数据上移。对话历史（`messagesBuffer`）跟随编排职责从 Bus 层迁至 Session 层的 `topicBaseJson`（ADR-0009 分层权限修订为此提供了规则基础）。Bus 层 Context 随之精简，去掉四个不再需要的字段：
>   - `messagesBuffer` + `messageOffsets` + `messageCount`：这三个服务于 ADR-0020 引入的 seq 版本控制机制——在旧并发模型下，同一 GTID 可能同时有多条消息在飞行，需要用 seq 编号和偏移索引区分各条消息的处理状态。新架构下编排器状态机串行推进（`waitingForTopic → waitingForDebateReplies → ...`），每个状态只等一件事，不存在并发抢占。对话历史本身已迁至 `topicBaseJson`。
>   - `pendingReqSeq`：同样服务于并发抢占——用于区分"当前在等的请求"和"已过时的旧请求"。串行模型下不需要。
>   - 这四个字段的底层驱动力（多前端并发 + ACK 同步）已在 FT0002-A 随用户系统移除。
> - **本质**：这不是"删几个字段"的局部优化，而是 Context 所有权随编排职责从 Bus 层向 Session 层迁移的结构性调整。Bus Context 不再"拥有"对话，只"使用"对话。
> - 修订依据：FT0002-B 设计，ADR-0009 分层权限修订。

---

## 背景

单 AI 对话（AiChat）需要携带对话历史请求 AI API。OpenAI 兼容 API 的请求体格式为：

```json
{"model":"...","messages":[{...},{...},...],"temperature":0.7}
```

其中 `messages` 数组包含完整对话历史（system prompt + 所有 user/assistant 消息），每轮请求都要全量发送。

需要决定：(1) 对话历史存在哪里 (2) 以什么格式存储 (3) 内部消息体如何传递。

---

## 决策

### 1. 对话历史存储在 Context

`AiChatContext` 中维护 `messagesBuffer` 字段，存储 `messages` 数组的 JSON 片段（不含外层 `[` `]`）：

```
Context 存的内容（示例）：
{"role":"system","content":"你是一个助手"},{"role":"user","content":"hi"},{"role":"assistant","content":"hello"}
```

- 首轮自动预置 system prompt
- `buildMessagesJson()` 负责添加 `[` `]` 外壳和新 user 消息
- `appendAssistantMsg()` 追加 assistant 消息

### 3. 消息体传递 JSON 字符串

`AiChatRequest.messagesJson` 直接携带 messages 数组 JSON 字符串。Adapter 将其拼入 HTTP body 的 `"messages"` 字段。

### 4. Context 静态内存

**AiChatContext**（Bus 层，2026-07-30 修订）：

```
AiChatContext（.mt 定义）：
  uint8[64]    modelName         定长 model 名
  uint8[128]   apiUrl            API 端点
  uint8[128]   apiKey            API 密钥
  double       temperature       温度参数
  AiIndex      aiIndex           AI 身份编号（0~7 为参辩 AI，0xFE=裁判，0xFF=无效）
  EoAddress    businessReplyAddr 回复目标地址（→ sessionDispatcherAddr_）
```

**已废弃字段**（2026-07-30）：

| 字段 | 原用途 | 废弃原因 |
|------|------|------|
| `messagesBuffer[16384]` | 16KB 对话历史缓冲区 | 历史上移至 Session 层 `topicBaseJson`（1MB） |
| `messageOffsets[256]` | seq→buffer 偏移索引 | 随 messagesBuffer 废弃 |
| `messageCount` | 消息计数 | 随 messagesBuffer 废弃 |
| `pendingReqSeq` | 请求版本号，用于抢占控制 | 新架构下无并发抢占场景 |

**新增字段**：

| 字段 | 用途 |
|------|------|
| `apiUrl` | 从 `AiChatConfigReq.payload` 解析写入 |
| `apiKey` | 从 `AiChatConfigReq.payload` 解析写入 |
| `aiIndex` | 从 `AiChatConfigReq.aiIndex` 写入，`AiChatResp` 携带回编排器 |
| `systemPrompt` | 从 `AiChatConfigReq.systemPrompt` 写入。每次发 API 请求时作为 `messages[0]` |

### 5. 消息流转与 seq 版本控制

```
AiChatBusinessReq 到达:
  ① allocateAndRecordSeq(ctx)         → 分配 seq，记录 messageOffsets
  ② buildMessagesJson + writeToContext → 写 buffer
  ③ sendAck(ctx, gtid, seq, content)   → AiChatMsgAck 确认送达
  ④ pendingReqSeq = seq               → 更新版本号
  ⑤ buildAiChatServiceReq(reqSeq=seq)  → 发 API 请求

AiChatServiceResp 到达:
  ① resp.reqSeq == pendingReqSeq? → 匹配: 正常处理
  ② resp.reqSeq != pendingReqSeq? → 过时: 丢弃（新请求已由后续用户消息触发）

正常处理:
  ③ allocateAndRecordSeq(ctx)         → assistant 消息也分配 seq
  ④ appendAssistantMsg                → 追加 assistant
  ⑤ pendingReqSeq = 0                 → 清除等待标记
  ⑥ send AiChatBusinessResp           → 回复 SessionData
```

**抢占机制**：在 `pendingReqSeq != 0`（等待回复）期间收到新用户消息时，立即写入 context、发送 Ack、发起**新的** API 请求（携带更新的 reqSeq）。旧 API 回复到达时因 reqSeq 不匹配被丢弃。不主动取消旧请求（HTTP 无法可靠中断）。

---

## 为什么不是其他方案

### 方案 A：结构化存储——按条存储，长度前缀

```
每条消息：[2B length][1B role_len][role bytes][content bytes]
```

**否决理由**：AiChatBus 发请求时需遍历所有消息逐条读出再序列化为 JSON——多做一次转换。而 JSON 存可以直接拼进 HTTP body。多 AI 讨论场景通过 `return1/2/3` 专用字段处理，也不需要此处存储的单条访问能力。

### 方案 B：Adapter 维护对话历史

**否决理由**：Adapter 是纯翻译器，不应维护业务状态。且 Adapter 崩溃后历史丢失，无法恢复。

### 方案 C：历史存硬盘

**否决理由**：第一期 CLI 对话无需持久化。后期可通过 TaskPool 后端切换（内存→mmap）实现，AiChatBus 代码不改。

---

## 影响

- **AiChatContext**：全静态定长字段。新增 `messageOffsets`（seq→buffer 偏移）、`messageCount`（消息计数）、`pendingReqSeq`（请求版本号）。废弃 `turnCount`、`stage`（AiChatStage 枚举已移除）
- **AiChatBus**：新增 `allocateAndRecordSeq()`（seq 分配 + 偏移记录）、`sendAck()`（AiChatMsgAck 确认）。状态管理从 stage 枚举迁移到 `pendingReqSeq` 版本检测
- **AiApiAdapter**：HTTP body 组装为字符串拼接，无 JSON 解析
- **会话状态**：由 `pendingReqSeq` 隐式表达——`== 0` 空闲，`!= 0` 等待回复。支持抢占：等待期间新消息立即发起新请求，旧响应按版本丢弃

---

## 与现有 ADR 的关系

- ADR-0009：Context 存储于 TaskPool，由 Business D 面 EO 独享读写——符合
- ADR-0010：AiChatBus 通过 `TaskType::AiChat` 绑定 AiChatContext——符合
- ADR-0014：消息头为 `gtidList + sourceAddress`——AiChatRequest/Response 遵循

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-16 | 初稿，采纳 |
| 2026-06-21 | 新增 businessReplyAddr、AiChatStage 字段；messagesBuffer 改为不含 [] 外壳；拆出 buildMessagesJson/writeMessagesToContext/appendAssistantMsg；增加状态机说明 |
| 2026-06-26 | 引入 seq 版本控制机制：新增 messageOffsets/messageCount/pendingReqSeq，废弃 stage/turnCount；新增 allocateAndRecordSeq/sendAck；支持抢占式请求（等待期间新消息立即发新 API 请求，旧响应按版本丢弃）；消息头 MsgHead→UserHead 更名，嵌入 UserInfo；新增 AiChatMsgAck 确认消息 |
