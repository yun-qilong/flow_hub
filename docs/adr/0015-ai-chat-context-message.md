# ADR-0015：单 AI 对话——Context 与消息体设计

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳（部分修订） | 2026-06-16 | 韵启龙 |

> **修订**：
> - `AiChatStage` 枚举已被 [ADR-0020](../adr/0020-seq-version-control.md) 废弃，改为 `pendingReqSeq` 版本号机制
> - `turnCount` 由 `messageCount` 取代（ADR-0020）
> - `businessReplyAddr` 引用的 `sourceAddress` 字段已被 [ADR-0024](../adr/0024-head-accesstype-reuse.md) 移除
> - 对话历史存储机制（messagesBuffer）和消息体传递 JSON 字符串的决策仍然有效

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

```
AiChatContext（.mt 定义）：
  uint8[64]    modelName         定长 model 名
  double       temperature       温度参数
  int32        messagesLen       已用字节数
  uint8[16384] messagesBuffer    16KB JSON 缓冲区（不含 [] 外壳）
  EoAddress    businessReplyAddr 回复目标地址（从 req.head.sourceAddress 写入，回复后清空）
  uint16[256]  messageOffsets    每条消息在 buffer 中的起始字节偏移（seq→offset）
  uint8        messageCount      当前消息总数（= 下一条消息的 seq）
  uint16       pendingReqSeq     当前等待回复的请求版本号，0 = 无等待
```

- `messageOffsets[seq]` 记录 seq 对应的消息在 `messagesBuffer` 中的起始字节偏移
  - seq=0（首条用户消息）：偏移 = system prompt 长度（~65 字节）
  - seq≥1：偏移 = 写入前 `messagesLen + 1`（逗号分隔符之后）
- `messageCount` 为 0-based，每次写入消息时 `allocateAndRecordSeq()` 先取值再自增
- `pendingReqSeq` 替代了原 `AiChatStage` 枚举：`== 0` 表示空闲，`!= 0` 表示等待回复
- 已废弃字段：`turnCount`（由 messageCount 取代）、`stage`（由 pendingReqSeq 取代）

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
