# alg00009：AiAgora Reset

| 属性 | 值 |
|------|-----|
| 编号 | alg00009 |
| 对应 Subfeature | FT0002-B |

编排器收到 `AiAgoraResetReq` 后的上下文重置流程。Reset 只清空 Session 层对话上下文，**保留配置**，使会话回到 `waitingForTopic` 态继续新话题。

> Reset **不重发** `AiChatConfigReq`：提示词与模型参数已保存在各 BusTask 的 `AiChatContext.systemPrompt` 等字段（见 `docs/construct/contexts.md`），Session 层 Reset 不影响 Bus 层配置。

## 1. Entry & State Check

编排器收到 `AiAgoraResetReq`（仅 `head`，无 payload）后按当前状态判定：

| 当前状态 | 行为 |
|----------|------|
| `unconfigured`（从未配置） | 回复 `AiAgoraResetResp{isSuccess=false}`，无可重置会话 |
| `configuring`（配置进行中） | 回复 `AiAgoraResetResp{isSuccess=false}`，配置未完成，打断会使 BusTask 分配状态混乱 |
| `waitingForTopic` | 执行清空（§2） |
| `waitingForDebateReplies` / `waitingForJudgeVerdict`（讨论中） | 执行清空（§2），放弃当前话题 |
| `dead` | 回复 `AiAgoraResetResp{isSuccess=false}` |

## 2. Clear Session Context

清空对话相关字段，保留配置字段：

**清空**：

| 字段 | 操作 |
|------|------|
| `topicBaseJson` | 置空 |
| `lastRoundResponses` | 全部置空 |
| `currentRound` | 置 0 |
| `pendingReplies` | 清零 |
| `state` | 置 `waitingForTopic` |

**保留**（配置不变，Bus 层 `AiChatContext` 不受影响）：

- `debateTaskIds[]`、`judgeTaskId`
- `maxRounds`、`maxResponseLength`、`maxCharPerTopic`、`timeoutMs`

## 3. Respond

回复 `AiAgoraResetResp`：

- `isSuccess` = `true`
- `estimatedTopicCount` = `floor(kTopicBaseJsonSize / maxCharPerTopic)`

`maxCharPerTopic` 在配置阶段计算并保存（alg00005 §1）。Reset 后 `topicBaseJson` 全空，`estimatedTopicCount` 回到初始值（与 `TaskConfigResp` 一致）。

## 4. 迟到的回复

讨论中 Reset（`waitingForDebateReplies` / `waitingForJudgeVerdict`）后，可能仍有在途的 `AiChatResp` 到达编排器。此时 `state` 已为 `waitingForTopic`，不处于收集阶段——编排器按 alg00006 的状态校验直接丢弃：不更新 `topicBaseJson`、不触发裁判、不发前端通知。

## 5. 消息路径

```
前端 → AiAgoraResetReq → AccessGateway（alg00002：D 面）→ SessionDispatcher（alg00003：按 TaskType）→ 编排器
编排器 → AiAgoraResetResp → SessionDispatcher → AccessGateway（查 GTID→Adapter 映射）→ 前端
```

## 6. Boundary Conditions

| 输入 | 行为 |
|------|------|
| `unconfigured` 态 Reset | `AiAgoraResetResp{isSuccess=false}` |
| `configuring` 态 Reset | `AiAgoraResetResp{isSuccess=false}` |
| 正常 Reset（`waitingForTopic`） | 清空上下文 → `AiAgoraResetResp{isSuccess=true, estimatedTopicCount}` |
| 讨论中 Reset | 放弃当前话题、清空 → `isSuccess=true`；迟到回复丢弃 |
| `dead` 态 Reset | `AiAgoraResetResp{isSuccess=false}` |

---

## 引用

- 消息：`AiAgoraResetReq` / `AiAgoraResetResp`（`docs/construct/messages.md`）
- 协议：`docs/feature/FT0002/frontend-protocol.md` §2.5/2.6、会话生命周期⑥
- 配置：`alg00005`（`maxCharPerTopic` 计算）
- 编排状态机：`alg00006`
- Context：`docs/construct/contexts.md`（`AiAgoraSessionContext`）
