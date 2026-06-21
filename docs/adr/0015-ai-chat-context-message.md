# ADR-0015：单 AI 对话——Context 与消息体设计

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-16 | 韵启龙 |

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
  int32        turnCount         轮次计数
  double       temperature       温度参数
  int32        messagesLen       已用字节数
  uint8[16384] messagesBuffer    16KB JSON 缓冲区（不含 [] 外壳）
  EoAddress    businessReplyAddr 回复目标地址（从 req.head.sourceAddress 写入，回复后清空）
  AiChatStage  stage             会话阶段（AwaitingUser / AwaitingServiceResp）
```

- 所有字段为定长类型，无 `std::string` / `std::vector`
- `businessReplyAddr` 在收到请求时写入，发送响应后清空，确保每轮独立
- `stage` 用于状态机校验，防止错序消息
- 缓冲区满时通知用户"对话容量已满"

### 5. 消息流转与会话状态

```
                   req.head.sourceAddress ──▶ ctx.businessReplyAddr (回复目标)
AwaitingUser ──(AiChatBusinessReq)──▶ messagesBuffer 追加 user ──▶ AwaitingServiceResp
                                                                          │
                                                                     (AiChatServiceResp)
                                                                          │
                                                                          ▼
AwaitingUser ◀── businessReplyAddr 清空 ◀── messagesBuffer 追加 assistant ◀──┘
```

AiChatBus 不持有固定的回复地址，回复目标跟随每条请求存入 Context。

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

- **AiChatContext**：全静态定长字段，新增 `businessReplyAddr`（回复目标地址）和 `stage`（AiChatStage 枚举，状态机校验）
- **AiChatBus**：消息组装为 `buildMessagesJson()`（纯拼接 + 写回） + `appendAssistantMsg()`（追加 assistant），入口 `handle()` 精简为路由 + 委托
- **AiApiAdapter**：HTTP body 组装为字符串拼接，无 JSON 解析
- **会话状态**：`AwaitingUser` ↔ `AwaitingServiceResp` 两阶段状态机，防止消息错序

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
