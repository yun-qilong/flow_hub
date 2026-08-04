# alg00011：AiChatBus 消息处理（AiChatReq）

| 属性 | 值 |
|------|-----|
| 编号 | alg00011 |
| 对应 Subfeature | FT0002-B |

AiChatBus 处理 `AiChatReq` 的全程：入向转发（→ Service 层）+ 出向回复（→ 编排器）。

## 1. 入向：收到 AiChatReq

编排器**批量**发出 `AiChatReq`（`busTaskIds` 含全部参辩 GTID，内容相同），Router 逐条改写为单元素后转发（alg00004）。AiChatBus 收到：

- `head.busTaskIds.at(0)` = 本 BusTask GTID
- `messagesJson` = 编排器组装的对话数组（仅 user/assistant，最后一条 user）

处理流程：

1. 按 GTID 从 TaskPool 取 `AiChatContext`；不存在 → 丢弃 + 日志。
2. 取 `AiChatContext.systemPrompt`（配置阶段写入，alg00010）。
3. 构造 API 请求 messages：`systemPrompt` 作为 `messages[0]` 插入，后接 `messagesJson`。
4. 从 `AiChatContext` 取 `apiUrl`/`apiKey`/`modelName`/`temperature`。
5. 构造 `AiChatServiceReq` → 发送 Service 层入口（ServiceGateway；`-F` 后为 ServiceScheduler）。

## 2. 出向：收到 AiChatServiceResp

Service 层（AiApiAdapter）完成 HTTP 调用后回 `AiChatServiceResp{head, success, content}`。

- `success=true`：构造 `AiChatResp{head, success=true, aiIndex=ctx.aiIndex, content}` → 发送 SessionDispatcher（回编排器，按 `sessionTaskId` 路由）。
- `success=false`：错误原因（HTTP 错误 / 网络错误 / 超时）打 syslog；构造 `AiChatResp{head, success=false, aiIndex=ctx.aiIndex}`（`content` 为空）→ 同样发送 SessionDispatcher。

`AiChatResp` 的 `aiIndex` 取自 `AiChatContext.aiIndex`，编排器据此匹配 `pendingReplies`（alg00006 §5）。

## 3. 失败表达

`AiChatResp` 用 `success` 字段表达调用结果（HTTP 错误 / 网络错误 / 超时统一归入失败，不细分）：

- `success=true`：`content` 为 AI 回答
- `success=false`：`content` 为空；**具体错误原因由 AiChatBus 打 syslog，不展示给前端**

编排器收到 `success=false` 时按"该 AI 本轮失败"处理（同缺答语义，见 alg00006 §5）。

## 引用

- 消息：`AiChatReq` / `AiChatResp` / `AiChatServiceReq` / `AiChatServiceResp`（`docs/construct/messages.md`）
- Context：`docs/construct/contexts.md`（`AiChatContext`）
- 上游组装：`alg00006`（编排器构造 `messagesJson` 并批量发出）
- 配置写入：`alg00010`（`AiChatContext` 字段来源）
