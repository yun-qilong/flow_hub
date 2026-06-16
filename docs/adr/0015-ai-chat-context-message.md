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

`AiChatContext` 中维护 `messagesBuffer` 字段，存储 `messages` 数组的 JSON 字符串（不含外层 `model` 和 `temperature`）：

```
Context 存的内容（示例）：
[{"role":"system","content":"你是一个助手"},{"role":"user","content":"hi"},{"role":"assistant","content":"hello"}]
```

### 2. AiChatBus 纯追加操作，不解析

- 新请求时从 Context 取出整段 messages JSON，拼入本轮新 `user` 消息
- 收到 AI 回复后把 `{"role":"assistant","content":"..."}` 追加到 Context 末尾
- 不解析、不遍历、不需要单条提取

### 3. 消息体传递 JSON 字符串

`AiChatRequest.messagesJson` 直接携带 messages 数组 JSON 字符串。Adapter 将其拼入 HTTP body 的 `"messages"` 字段。

### 4. Context 静态内存

```
AiChatContext（.mt 定义）：
  uint8[64]   modelName        定长 model 名
  int32       turnCount        轮次计数
  double      temperature      温度参数
  int32       messagesLen      已用字节数
  uint8[16384] messagesBuffer  16KB JSON 缓冲区
```

- 所有字段为定长类型，无 `std::string` / `std::vector`
- 缓冲区满时通知用户"对话容量已满"
- 后期预留自动压缩机制（调 AI 压缩历史后覆盖）

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

- **AiChatContext**：从实验性结构（含 `std::string`）改为全静态定长字段
- **AiChatBus**：消息组装逻辑为纯字符串拼接 + memcpy
- **AiApiAdapter**：HTTP body 组装为字符串拼接，无 JSON 解析（nlohmann/json 只用于构造外层对象）
- **单 AI Chat 与多 AI 讨论复用**：多 AI 讨论在 Context 中增加 `return1/2/3` 字段，消息链路不变

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
