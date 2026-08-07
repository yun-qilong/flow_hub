# alg00006：AiAgora Topic Orchestration

| 属性 | 值 |
|------|-----|
| 编号 | alg00006 |
| 对应 Subfeature | FT0002-B |

---

编排器收到 `AiAgoraChatReq` 后的话题编排流程。单 AI 直通转发，多 AI 自动推进多轮辩论。

## 术语

- **`topicBaseJson`**：编排器在 Session Context 中持有的**累积对话历史存储**（静态缓冲，容量受 Context 约束，见 alg00005/contexts）。跨话题、跨轮次持续追加——user 驱动消息（辩论模式带 `topic:` / `judge:` 前缀，单 AI 为原始内容）与 assistant 回答（辩论模式为合并串，单 AI 为原始回答）交替累积。§2 的容量检查针对此缓冲。已用长度由 `topicBaseJsonSize` 维护（初始 `2`，即空数组文本 `[]`），追加时不解析全量，仅按字节在 `]` 前插入新元素。
- **`messagesJson`**：每次实际发送的**请求载荷**（`AiChatReq.messagesJson` 字段），是 `topicBaseJson` 当前完整内容的 JSON 数组。`messagesJson = topicBaseJson` 表示"载荷直接取累积历史的当前状态"——追加最新驱动消息后，将**全量历史**一次发出，而非增量。**例外**：裁判的载荷单独构造，仅含当前话题 topic + 当轮回答，不携带历史（见 7.1）。

## 1. Entry & State Check

仅 `waitingForTopic` 态接受 `AiAgoraChatReq`。非此态回复 `AiAgoraChatResp{isComplete=true, errorCode=InvalidState, currentState=当前状态}`，不处理 `content`。前端据此同步状态。

## 2. Context Capacity Check

检查 `topicBaseJson` 剩余空间是否 ≥ `maxCharPerTopic`。不足则回复 `AiAgoraChatResp{errorCode=ContextFullAtStart}`，不消耗 `content`。

## 3. Build messagesJson

编排器组装 `messagesJson`（`AiChatReq.messagesJson`，发送给 `AiChatBus` 的请求载荷），只含 `role: user/assistant` 消息。**不含 system**——system 由 AiChatBus 从 `AiChatContext.systemPrompt` 插入 `messages[0]`，编排器不提供（辩手、裁判一致）。

内容组装由 `hasJudge` 区分两种模式（约束保证 `hasJudge=false` ⟺ `aiCount=1`）：

**单 AI（`hasJudge=false`）**：不加任何前缀，消息即原始输入/回答（利于 API 缓存命中），见 3.1。

**辩论（`hasJudge=true`）**：使用结构化标记体系（编号 0-based，与 `aiIndex` 一致；格式在配置阶段写入各 AI 的 systemPrompt，见 alg00005）：

| 消息 | role | 语义 |
|------|------|------|
| `topic: {content}` | user | 新话题，AI 据此完整作答 |
| `judge: {judgeContent}` | user | 裁判对上一轮的评判，AI 据此调整观点继续讨论 |
| 合并回答串 `AI0：{回答0}\n AI1：{回答1}\n ...` | 按接收方 | 各回答按 `aiIndex` 合并、`AI{i}：` 前缀标识、`\n` 分隔 |

**合并回答串的 role**——它是**同一份字符串**，按接收方以不同 role 写入：

| 接收方 | role | 说明 |
|------|------|------|
| 辩手（追加 `topicBaseJson` 回灌，§5/§6） | assistant | 作为 assistant 消息 content 累积在辩手历史 |
| 裁判（裁判载荷，§7.1） | user | 作为唯一一条 user 身份的 content 发出（裁判据此评判） |

`[FINAL_ROUND]` 标记（user）在达轮次上限时插入裁判载荷头部（§7.1）。

### 3.1 单 AI 模式（`hasJudge=false`）

**不加任何前缀**，消息内容即原始输入/回答（利于 API 缓存命中）。

首轮：将 `content` **原样**作为 user 消息追加到 `topicBaseJson` 末尾，`messagesJson = topicBaseJson`。

单 AI 无轮次概念：发送 → 收回答 → 将回答**原样**作为 assistant 消息追加到 `topicBaseJson` → 回复前端（`responses` = 原始回答）→ 结束。

### 3.2 多 AI 辩论 — 首轮

`currentRound` 置 1。将 `content` 以 `{"role":"user","content":"topic: {content}"}` 追加到 `topicBaseJson` 末尾，`messagesJson = topicBaseJson`。

### 3.3 多 AI 辩论 — 后续轮次

将上一轮裁判评判正文（编排器暂存）以 `{"role":"user","content":"judge: {裁判评判正文}"}` 追加到 `topicBaseJson` 末尾，`messagesJson = topicBaseJson`。

上一轮的回答已作为合并后的 assistant 消息在收集完成后追加到 `topicBaseJson`（见步骤 6）。

每次新的 `AiAgoraChatReq` 触发一个新话题的首轮（`topic:`）。同一话题内的轮次切换由编排器自动推进，无需前端介入。

## 4. Fan-out to Debaters

构造**一条** `AiChatReq` 发往 Router：`head.busTaskIds` = 全部参辩 GTID 列表 `debateTaskIds[0..aiCount-1]`，`messagesJson` = 步骤 3 构造的对话数组。

Router 遍历 `busTaskIds`，复制消息并将副本改写为单元素列表后逐条转发（alg00004，通用 fan-out 逻辑）：

- 每条 `head.busTaskIds` = `[debateTaskIds[i]]`
- `messagesJson` = 相同（复制）

所有辩手收到**相同**的 messagesJson。system 消息由各自 AiChatBus 根据自身 `AiChatContext.systemPrompt` 独立插入。

## 5. Collect Responses

设置 `pendingReplies` bitset（低 `aiCount` 位置 1）。

每收到 `AiChatResp`：
- 校验 `aiIndex` 在合法范围（0 ~ `aiCount-1`）
- 若 `success=false`：该 AI 本轮失败 → 同缺答处理（见下）
- 清除 `pendingReplies` 对应位
- 将 `content` 缓存到 `lastRoundResponses[aiIndex]`
- 全部位清零 → 本轮收集完成

晚到/重复响应处理：`AiChatResp` 到达时仅当处于等待态且对应 pending 位仍置位才处理；否则视为晚到响应，忽略（状态已复位）。

单 AI 模式（`aiCount=1`）仅使用 `pendingReplies` bit0，收集完成即处理，不依赖 `lastRoundResponses` 缓存。

**编排器保证每个 AI 都不缺答或失败**：任一 AI 超时未答，或收到 `success=false`（AiChatBus 检测到 API 失败，见 alg00011）→ **本轮辩论失败**，回复 `AiAgoraChatResp{isComplete=true, errorCode=NetworkTimeout}`，结束当前 topic（不拼接、不进下一轮、不调用裁判）。


收集完成后（全部收齐），按模式分别处理：

**单 AI 模式（`hasJudge=false`）**——只有一个回答，不合并、不加 `AI{i}：` 前缀，得到的字符串就是原始回答。该字符串有两个作用：

1. **追加到 `topicBaseJson`**：作为 assistant 消息的 `content` 进入累积对话历史。
2. **发给前端**：作为 `AiAgoraChatResp.responses` 随消息下发，前端直接展示原始回答。

**辩论模式（`hasJudge=true`）**——将各回答按 `aiIndex` 顺序合并为一条 `"AI0：{回答0}\nAI1：{回答1}\n..."`，得到合并串。该字符串有两个作用：

1. **追加到 `topicBaseJson`**：作为 assistant 消息的 `content` 进入累积对话历史，供后续轮次/话题回灌给 AI 时可见。
2. **发给前端**：作为 `AiAgoraChatResp.responses` 随消息下发，前端直接展示本轮所有回答。

## 6. Frontend Notification — 辩手回复

收集完成后，将本轮回答追加到 `topicBaseJson`（辩论模式为合并后的 assistant 消息 `AI0：...\nAI1：...`；单 AI 为原始回答）。发送 `AiAgoraChatResp`：

- `hasResponses=true`
- `isComplete=false`（是否真正结束由后续裁判评判决定）
- `responses` = 回答字符串（辩论模式为合并串、与回灌 assistant content 一致；单 AI 为原始回答、无前缀）

### 上下文写满

若追加本轮回复后 `topicBaseJson` 超出容量，回复 `AiAgoraChatResp{errorCode=ContextFullMidRound, isComplete=true}`，终止。

## 7. Judge Invocation

`hasJudge=true` 时，**每轮辩手回复收集完成后**调用裁判。

### 7.1 构建裁判消息

向 `judgeTaskId` 发送 `AiChatReq`。裁判的 messagesJson **仅包含当前话题的 `topic` 与当轮各辩手的回答，不携带之前轮次的回答**，各消息均以 **user 身份**写入（见 §3 消息身份表）：

```
[{"role":"user","content":"topic: {当前话题}"},
 {"role":"user","content":"AI0：{当轮回答0}\nAI1：{当轮回答1}\n..."}]
```

若 `currentRound >= maxRounds`，在数组头部插入 `[FINAL_ROUND]` 标记（包装为 user 消息），提示裁判输出最终总结。未达上限则不插入。

裁判的 system prompt 已在 config 阶段存储于裁判 BusTask 的 `AiChatContext.systemPrompt`，由 AiChatBus 在发送 AI API 请求时插入数组头部。AiAgora 构建的 messagesJson 全程不含 system 角色消息，Bus 层零拷贝透传。

### 7.2 解析裁判回复

等待裁判的 `AiChatResp`（`aiIndex=kJudgeIndex`）。若 `success=false`（裁判 API 失败）：同缺答处理——回复 `AiAgoraChatResp{isComplete=true, errorCode=NetworkTimeout}`，结束 topic。否则解析 `content`：

1. 读首行，匹配 `[AGREE]` 或 `[DISAGREE]`
2. 剥离首行，剩余文本为评判正文
3. `consensusReached = (首行 == "[AGREE]")`

**每次裁判回复均发回前端**，正文以 `裁判的裁决：` 前缀写入 `AiAgoraChatResp.responses`。

### 7.3 后续动作

**`[AGREE]`**（含总结正文）：
- 将总结正文以 `{"role":"user","content":"judge: {总结正文}"}` 追加到 `topicBaseJson`（以 user 身份写入，格式与 §3.3 后续轮次的 `judge:` 消息一致）
- 发送 `AiAgoraChatResp{isComplete=true, endReason=JudgeApproved}`，`responses` = `裁判的裁决：{总结正文}`
- 话题结束

> 注：话题终止后 `topicBaseJson` 末尾为裁判总结的 user 消息；新话题首轮（§3.2）再追加 `topic:` user 消息，形成**连续两条 user 消息**——AI API 允许连续 user 消息，合法。

**`[DISAGREE]` + 已达 `[FINAL_ROUND]`**：
- 虽然判定不一致，但裁判仍输出总结正文（≤ `maxResponseLength` 字）
- 将总结正文以 `{"role":"user","content":"judge: {总结正文}"}` 追加到 `topicBaseJson`（以 user 身份写入）
- 发送 `AiAgoraChatResp{isComplete=true, endReason=MaxRoundsReached}`，`responses` = `裁判的裁决：{总结正文}`
- 话题结束

**`[DISAGREE]` + 未达 `[FINAL_ROUND]`**：
- 正文为评判意见（≤ `maxResponseLength/10` 字），简述分歧点
- 发送 `AiAgoraChatResp{isComplete=false}`，`responses` = `裁判的裁决：{评判意见}`
- 暂存评判意见供步骤 3.3 追加为 `judge:` user 消息（供辩手参考调整论点）
- `currentRound++`，回到步骤 3.3

裁判缺答（超时）同样判定本轮失败：**无回复可写，不追加到 `topicBaseJson`**，直接回复 `AiAgoraChatResp{isComplete=true, errorCode=NetworkTimeout}`，结束 topic。

## 8. Termination

话题结束后状态回到 `waitingForTopic`，可接收下一个 `AiAgoraChatReq`。

终止原因汇总：

| 条件 | endReason | errorCode |
|------|-----------|-----------|
| 单 AI 直接回答 | NoJudge (1) | NoError (0) |
| 裁判判定一致 | JudgeApproved (2) | NoError (0) |
| 达到轮次上限 | MaxRoundsReached (3) | NoError (0) |
| 任一 AI（含裁判）缺答（超时） | — | NetworkTimeout (1) |
| 上下文空间不足（开始时） | — | ContextFullAtStart (2) |
| 上下文写满（辩论中） | — | ContextFullMidRound (3) |
