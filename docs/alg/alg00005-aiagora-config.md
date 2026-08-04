# alg00005：AiAgora Configuration

| 属性 | 值 |
|------|-----|
| 编号 | alg00005 |
| 对应 Subfeature | FT0002-B |

---

编排器收到 `TaskConfigReq` 后的配置准备流程。

## 1. 整体流程

```
TaskConfigReq
   │
   ▼
① 校验消息体 ──────失败──▶ TaskConfigResp{isSuccess=false}
   │ 通过
   ▼
② 申请 BusTask GTIDs ──失败──▶ TaskConfigResp{isSuccess=false}
   │ 成功
   ▼
③ 逐条分发 AiChatConfigReq（提示词拼接见 §4.1）
   │
   ▼
④ 等待全部 AiChatConfigResp ──超时/任一失败──▶ 回收 GTID，TaskConfigResp{isSuccess=false}
   │ 全部成功
   ▼
TaskConfigResp{isSuccess=true, estimatedTopicCount}
```

| 步骤 | 内容 | 详见 |
|------|------|------|
| ① | 解析 `payload`、校验字段与约束 | §2 |
| ② | 批量申请 `aiCount + (hasJudge ? 1 : 0)` 个 BusTask GTID | §3 |
| ③ | 对每个 AI 逐条发 `AiChatConfigReq`（含拼接好的 systemPrompt） | §4 |
| ④ | 收齐全部 `AiChatConfigResp` 后回 `TaskConfigResp` | §5 |

## 2. 校验消息体

从 `payload` JSON 中提取 `aiCount`、`hasJudge`、`maxRounds`、`maxResponseLength`、`timeoutMs` 及 `configs[]`。校验必填字段，缺失则直接回复 `TaskConfigResp{isSuccess=false}`。

校验约束（**双向绑定** `hasJudge == (aiCount > 1)`）：
- 若 `hasJudge=false`，要求 `aiCount=1`（单 AI 对话，无裁判）
- 若 `hasJudge=true`，要求 `aiCount>1`（辩论模式，裁判存在时辩手数 ≥ 2，不允许"1 辩手 + 1 裁判"）

违反约束则回复 `TaskConfigResp{isSuccess=false}`。

计算 `maxCharPerTopic = ceil(maxResponseLength × aiCount × maxRounds × kContextRedundancyFactor)`。
冗余因子 `kContextRedundancyFactor = 1.1`，覆盖 AI 可能超出提示词字数限制和编排器拼接的额外文字。

## 3. 申请 BusTask GTIDs

构造 `BusTaskCreateReq`（详见 alg00007）：`taskTypes` 均为 AiChat 类型，共 `aiCount + (hasJudge ? 1 : 0)` 个。裁判与辩手类型相同，仅后续 `aiIndex` 区分。

收到 `BusTaskCreateResp{isSuccess=true}` 后，`head.busTaskIds` 与 `taskTypes` 一一对应，按序存入 `debateTaskIds[]` 和 `judgeTaskId`。失败则回复 `TaskConfigResp{isSuccess=false}`。

## 4. 分发 AiChatConfigReq

对每个 AI **逐条**发送 `AiChatConfigReq`（**必须逐条**：`aiIndex`/`systemPrompt`/`payload` 逐 AI 不同，不能捆绑后依赖 Router 拆开——Router fan-out 只复制消息、不改字段）：

- 参辩 AI：拼接 `systemPrompt = configs[i].systemPrompt + 辩手 prompt + 编号标识`，带 `aiIndex=i` 和 `payload`
- 裁判：拼接 `systemPrompt = configs[aiCount].systemPrompt + 裁判 prompt`，`aiIndex=kJudgeIndex`

角色身份由 `configs[i].systemPrompt` 定义（如"你是法律专家"），辩手 prompt 描述工作流规则，编号标识（如"你是第 i 号辩手"）使 AI 能在辩论记录中识别自身发言。

### 4.1 提示词拼接

根据 `aiCount`、`hasJudge`、`maxRounds`、`maxResponseLength` 动态拼接 prompt 文本。辩手和裁判收到**不同的 prompt**——辩手收辩论规则，裁判收评判规则。用后即弃，不保存在 Session Context。

#### 4.1.1 辩手 Prompt

`hasJudge` 决定辩手 prompt 的内容。prompt 需包含以下信息，实际措辞在实现时调优：

**分支 A：`hasJudge=true`（多 AI 辩论）**

需传达的信息：
- 场景：参与一场由 `{aiCount}` 个 AI 组成的辩论，设有一名裁判在全部轮次结束后给出最终评判
- 轮次规则：每话题最多 `{maxRounds}` 轮，每轮所有 AI 依次发言，发言完毕后可看到包括自己在内所有 AI 的发言，然后进入下一轮
- 字数限制：每轮每个 AI 回答不超过 `{maxResponseLength}` 字
- 行为期望：以自身专业角度充分发表观点，回应其他 AI 的质疑，推动讨论达成共识
- 编号：每个辩手 AI 在分发时额外追加序号标识（如"你是第 {i} 号辩手"），使其在后续辩论中能识别自身与其他辩手的发言
- 上下文格式：`topic:` 开头的 user 消息为新话题，据此完整作答；`judge:` 开头的 user 消息为裁判对上一轮的评判，据此调整观点继续讨论；assistant 消息为所有辩手回答的合并，每条以 `AI{i}：` 前缀标识、`\n` 分隔
- 身份识别：你是 `AI#{i}`，通过 content 中的 `AI{i}：` 前缀区分自己与他人的回答，不回引自己的编号
- 输出约束：回答正文**不要**以 `AI{i}：` 前缀开头（前缀由编排器统一添加）

**分支 B：`hasJudge=false`（单 AI 对话，`aiCount=1`）**

需传达的信息：
- 场景：与用户进行一对一对话，根据问题直接给出回答
- 字数限制：每次回答不超过 `{maxResponseLength}` 字
- 无上下文标记：编排器透传原始输入/回答，不加任何前缀
- 编号：无需追加（仅一个 AI）

#### 4.1.2 裁判 Prompt

仅 `hasJudge=true` 时生成。裁判收到的是评判规则，而非辩论规则。

需传达的信息：
- 角色：辩论裁判，不参与辩论，在每轮辩论结束后对各 AI 发言进行评判
- 上下文：裁判每次仅收到当前话题的 `topic` 与当轮各辩手回答（合并的 assistant 消息，`AI{i}：` 前缀标识），**不携带之前轮次的回答**
- 入向约定：编排器在 messagesJson 中插入 `[FINAL_ROUND]` 标记表示达轮次上限，裁判据此切换输出模式
- 评判维度：
  1. 立场明确性：各方是否清晰表达了核心观点和论据
  2. 交锋充分性：各方是否回应了彼此的质疑，分歧点是否被实质讨论而非各说各话
  3. 收敛程度：多轮辩论后各方观点是否趋于一致；若仍有分歧，是根本性对立还是角度差异
  4. 建议可用性：综合各方论述，能否提炼出对用户有实际操作价值的建议
- 输出格式约定：
  - 首行固定为 `[AGREE]` 或 `[DISAGREE]`，独占一行
  - 第二行起为正文
- 输出规则：
  - 收到 `[FINAL_ROUND]` 标记：输出 `[AGREE]` + 最终总结（≤ `maxResponseLength` 字），无论是否一致都做总结
  - 未收到 `[FINAL_ROUND]` 且判定一致：输出 `[AGREE]` + 总结（≤ `maxResponseLength` 字）
  - 未收到 `[FINAL_ROUND]` 且判定不一致：输出 `[DISAGREE]` + 自然语言评判（≤ `maxResponseLength/10` 字），简述分歧点

#### 4.1.3 拼接流程

1. 在栈上分配临时 buffer，约 200~800 字节。
2. 辩手 prompt：根据 `hasJudge` 选分支，替换 `{aiCount}`、`{maxRounds}`、`{maxResponseLength}` 为十进制字符串。
3. 裁判 prompt：仅 `hasJudge=true` 时生成，无占位符，直接写入。
4. 得到辩手 prompt 和裁判 prompt 两个独立字符串。

#### 4.1.4 使用与丢弃

分发时，辩手和裁判各自拼接自己的 prompt：
- 参辩 AI：`systemPrompt = configs[i].systemPrompt + 辩手 prompt + 编号标识`（编号用于 AI 在辩论中区分自身和他人发言）
- 裁判：`systemPrompt = configs[aiCount].systemPrompt + 裁判 prompt`

全部 `AiChatConfigReq` 发送完成后两个 prompt 均丢弃。

## 5. 等待确认并回复

设置 `pendingReplies` bitset，启动超时。每收到成功响应清除对应位。全部成功回复 `TaskConfigResp{isSuccess=true}`。超时或任一失败则回收全部 BusTask GTID，回复 `TaskConfigResp{isSuccess=false}`。

## 引用

- 消息：`TaskConfigReq` / `TaskConfigResp` / `AiChatConfigReq` / `AiChatConfigResp` / `BusTaskCreateReq` / `BusTaskCreateResp`（`docs/construct/messages.md`）
- BusTask 批量申请：`alg00007`
- Bus 层配置初始化：`alg00010`（AiChatBus 收到 `AiChatConfigReq` 后的处理）
- 路由：`alg00003`（SessionDispatcher）、`alg00004`（Router）
- 协议：`docs/feature/FT0002/frontend-protocol.md` §2.1/2.2
