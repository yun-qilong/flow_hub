# Context 结构定义

> Session 层和 Bus 层的 Context 字段及用途。

---

## AiAgoraSessionContext（Session 层）

编排器（AiAgora）维护的 Session 级上下文。一个 Session Task 对应一份。

| 字段 | 类型 | 说明 |
|------|------|------|
| `debateTaskIds` | `GTID[kMaxDebateAICount]` | 参辩 AI 的 BusTask GTID，按 `aiIndex` 顺序排列 |
| `judgeTaskId` | `GTID` | 裁判 BusTask GTID。无裁判时 = `kInvalidGtid` |
| `maxRounds` | `uint8_t` | 每话题讨论轮次上限 |
| `maxResponseLength` | `uint16_t` | 每轮每 AI 回答字数上限 |
| `maxCharPerTopic` | `uint32_t` | 每话题预估最大字符数 |
| `timeoutMs` | `uint32_t` | 编排器通用超时（毫秒） |
| `pendingReplies` | `uint16_t` | bitset，每 bit 对应一个 `aiIndex` 的待回复 AI |
| `topicBaseJson` | `uint8_t[kTopicBaseJsonSize]` | 10MB。对话历史累积缓冲（辩论/单 AI 均用）。Reset 时清空 |
| `lastRoundResponses` | `uint8_t[kMaxDebateAICount][kResponseJsonSize]` | 上轮各 AI 回话缓存 |
| `currentRound` | `uint8_t` | 当前轮次，用于与 `maxRounds` 比较 |
| `state` | 枚举 | 状态机 |

> Session Context 不保存提示词。提示词由前端通过 `TaskConfigReq` 发来，AiAgora EO 填充工作流描述后直接发给 Bus 层 AiChat EO，此后提示词完全保存在 AiChat EO 的 `AiChatContext.systemPrompt` 中。

状态机流转逻辑见算法文档。

---

## AiChatContext（Bus 层）

单个 BusTask（一个 AI 代理人）的上下文。每个参辩 AI 和裁判各自一份。

| 字段 | 类型 | 说明 |
|------|------|------|
| `modelName` | `uint8_t[kModelNameSize]` | 模型名 |
| `apiUrl` | `uint8_t[kApiUrlSize]` | API 端点 |
| `apiKey` | `uint8_t[kApiKeySize]` | API 密钥 |
| `temperature` | `double` | 温度参数。来自 `AiChatConfigReq.payload` |
| `aiIndex` | `AiIndex` | AI 身份编号。来自 `AiChatConfigReq.aiIndex` |
| `systemPrompt` | `uint8_t[kSystemPromptSize]` | 该 AI 的完整 system prompt。来自 `AiChatConfigReq.systemPrompt`。每次发 API 请求时作为 `messages[0]` |

### 已废弃字段（2026-07-30）

以下字段随 ADR-0015 修订废除，详见该 ADR：

| 字段 | 废弃原因 |
|------|------|
| `messagesBuffer` | 对话历史上移至 Session 层 `topicBaseJson` |
| `messageOffsets` | 随 `messagesBuffer` 废弃 |
| `messageCount` | 随 `messagesBuffer` 废弃 |
| `pendingReqSeq` | 新架构下状态机串行，无并发抢占场景 |

---

## 常量

| 常量 | 值 | 说明 |
|------|------|------|
| `kInvalidGtid` | `0x0000` | 无效 GTID |
| `kJudgeIndex` | `0xFE` | 裁判 AI 身份编号 |
| `kMaxDebateAICount` | `8` | 最大参辩 AI 数 |
| `kTopicBaseJsonSize` | `10485760` | topicBaseJson 大小（10MB） |
| `kSystemPromptSize` | `4096` | systemPrompt 大小 |
| `kResponseJsonSize` | `4096` | 单个 AI 回话缓存大小 |
| `kModelNameSize` | `64` | 模型名字段大小 |
| `kApiUrlSize` | `128` | API URL 字段大小 |
| `kApiKeySize` | `128` | API Key 字段大小 |
| `kContextRedundancyFactor` | `1.1` | 容量预估冗余因子 |

## 类型

| 类型 | 定义 | 说明 |
|------|------|------|
| `AiIndex` | `uint8_t` | AI 身份编号类型。参辩 AI 从 0 顺序编号，裁判固定 `kJudgeIndex` |
