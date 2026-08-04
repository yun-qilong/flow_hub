# FT0002-B — 前端通信协议

> 前端 ↔ FlowHub 消息交互规范。所有消息经 Adapter ↔ Gateway 透传，前端无需感知内部路由。

---

## 一、会话生命周期

```
① 前端 → TaskCreateReq → 获得 sessionTaskId
② 前端 → TaskConfigReq → 配置 AI 参数 → 获得 estimatedTopicCount
③ 前端 → AiAgoraChatReq → 发起话题
④ 前端 ← AiAgoraChatResp ← 每轮结果（可能多次）
⑤ 重复 ③~④，直到 isComplete=true 或 errorCode≠0
⑥ 前端 → AiAgoraResetReq → 清上下文 → 可回到 ③
```

---

## 二、消息定义

### 2.1 TaskConfigReq

方向：前端 → 后端

```json
{
  "aiCount": 1,
  "hasJudge": false,
  "maxRounds": 5,
  "maxResponseLength": 500,
  "timeoutMs": 30000,
  "configs": [
    {
      "apiUrl": "https://api.example.com",
      "apiKey": "sk-...",
      "model": "model-name",
      "systemPrompt": "你是一个助手",
      "temperature": 0.7
    }
  ]
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `aiCount` | int | ✅ | 参辩 AI 数量（1~8） |
| `hasJudge` | bool | ✅ | 是否有裁判 |
| `maxRounds` | int | ✅ | 每话题最大讨论轮次（≥1） |
| `maxResponseLength` | int | ✅ | 每轮每 AI 回答的字数上限 |
| `timeoutMs` | int | ✅ | 编排器通用超时（毫秒） |
| `configs` | array | ✅ | 长度 = `aiCount + (hasJudge?1:0)`。前 `aiCount` 个为参辩 AI，最后一个为裁判 |

**`configs[i]` 字段**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `apiUrl` | string | ✅ | AI API 端点 |
| `apiKey` | string | ✅ | API 密钥（HTTP Header `Authorization: Bearer <key>`） |
| `model` | string | ✅ | 模型名 |
| `systemPrompt` | string | ✅ | 角色提示词。编排器会自动追加辩论工作流说明 |
| `temperature` | float | ✅ | 0.0~2.0 |

**约束**：
- 仅在 Session 未配置时接受（`unconfigured` 态）。重复发送会被拒绝
- 必填字段缺失 → `TaskConfigResp{isSuccess=false}`
- `configs[]` 长度必须 = `aiCount + (hasJudge?1:0)`
- `hasJudge` 与 `aiCount` **双向绑定**：`hasJudge=false` ⟺ `aiCount=1`（单 AI 无裁判）；`hasJudge=true` ⟺ `aiCount>1`（辩论必有裁判，不允许"1 辩手 + 1 裁判"）

### 2.2 TaskConfigResp

方向：后端 → 前端

| 字段 | 类型 | 说明 |
|------|------|------|
| `isSuccess` | bool | `false` = 配置失败（字段缺失/校验不通过） |
| `estimatedTopicCount` | uint16 | 预估可支持的完整话题数。公式：`floor(10MB / maxCharPerTopic)` |

### 2.3 AiAgoraChatReq

方向：前端 → 后端

| 字段 | 类型 | 说明 |
|------|------|------|
| `content` | string | 用户输入的新问题。**无需携带上下文**——FlowHub 在编排器维护上下文 |

**约束**：
- 仅在 `waitingForTopic` 态接受
- 上下文容量不足时返回 `AiAgoraChatResp{errorCode=ContextFullAtStart}`
- 提示词（`systemPrompt`）已在 `TaskConfigReq` 中配置，编排器自动拼装完整 messages JSON

### 2.4 AiAgoraChatResp

方向：后端 → 前端

| 字段 | 类型 | 说明 |
|------|------|------|
| `isComplete` | bool | `true` = 话题已结束，可发新话题；`false` = 等待更多消息 |
| `hasResponses` | bool | `true` → `responses` 有效 |
| `endReason` | uint8 | 仅在 `isComplete=true` 时有效 |
| `errorCode` | uint8 | 非 0 表示异常终止 |
| `currentState` | uint8 | 编排器当前状态。0=Unconfigured, 1=Configuring, 2=WaitingForTopic, 3=WaitingForDebateReplies, 4=WaitingForJudgeVerdict, 5=Dead |
| `responses` | string | 辩论：合并串（`AI{i}：` 前缀、`\n` 分隔；裁判内容 `裁判的裁决：` 前缀）；单 AI：原始回答无前缀。前端直接展示 |

**`endReason` 值**：

| 值 | 含义 | 触发条件 |
|:--:|------|------|
| 1 | NoJudge | 无裁判，单 AI 直接回答 |
| 2 | JudgeApproved | 裁判判定讨论一致 |
| 3 | MaxRoundsReached | 讨论达到轮次上限 |

**`errorCode` 值**：

| 值 | 含义 | 触发条件 |
|:--:|------|------|
| 0 | NoError | 正常 |
| 1 | NetworkTimeout | 任一 AI（含裁判）回复超时 → 本轮失败，话题终止 |
| 2 | ContextFullAtStart | 上下文空间不足以开始新话题 |
| 3 | ContextFullMidRound | 讨论中上下文写满 |
| 4 | Other | 其他异常 |
| 5 | InvalidState | 编排器不在 `waitingForTopic` 态，无法接受聊天请求（如尚未配置或正在进行其他话题） |

**`responses` 格式**：辩论模式——编排器按 `aiIndex` 顺序拼接各回答，每条以 `AI{i}：` 前缀标识、`\n` 分隔；裁判内容以 `裁判的裁决：` 前缀标识。单 AI——原始回答，无前缀。前端直接展示整个字符串。

**前端接收逻辑**：
```
if (hasResponses && !isComplete)  → 展示 responses，等待下一轮
if (hasResponses && isComplete)   → 展示 responses，话题结束（endReason=1）
if (!hasResponses && isComplete)  → 展示结束原因（endReason=2/3 或 errorCode）
if (errorCode != 0)               → 展示错误，话题终止
```

### 2.5 AiAgoraResetReq

方向：前端 → 后端

| 字段 | 类型 | 说明 |
|------|------|------|
| （无） | — | 仅 `UserHead head` |

用于主动清空当前 Session 的上下文，回到 `waitingForTopic` 态。

### 2.6 AiAgoraResetResp

方向：后端 → 前端

| 字段 | 类型 | 说明 |
|------|------|------|
| `isSuccess` | bool | 清空成功 |
| `estimatedTopicCount` | uint16 | 清空后预估可支持的完整话题数 |

---

## 三、提示词规范

前端在 `systemPrompt` 中应包含：

1. **基本角色设定**：AI 的行为、语气、知识范围
2. **每次回答字数约束**（建议）：与 `maxResponseLength` 一致。编排器会自动告知 AI 其身份编号和辩论规则，前端无需写这些

编排器会在 `systemPrompt` 后自动追加工作流说明（身份编号、辩论流程、裁判规则等）。

---

## 四、完整交互时序

```
前端                     FlowHub
 │                          │
 │──TaskCreateReq──────────▶│  ① 获取 Session GTID
 │◀─TaskCreateResp─────────│
 │                          │
 │──TaskConfigReq──────────▶│  ② 配置 AI 参数
 │◀─TaskConfigResp─────────│     (estimatedTopicCount)
 │                          │
 │──AiAgoraChatReq─────────▶│  ③ 发起话题（仅发问题，不携带上下文）
 │                          │
 │◀─AiAgoraChatResp────────│  ④ 参辩回复
 │   hasResponses=true      │
 │   isComplete=false       │
 │                          │
 │◀─AiAgoraChatResp────────│  ⑤ 裁判判决（如有）
 │   hasResponses=false     │
 │   isComplete=true        │
 │                          │
 │──AiAgoraChatReq─────────▶│  ⑥ 新话题...
 │                          │
 │──AiAgoraResetReq────────▶│  ⑦ 清上下文
 │◀─AiAgoraResetResp───────│
```
