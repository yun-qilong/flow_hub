# alg00010：AiChatBus 配置初始化（AiChatConfigReq）

| 属性 | 值 |
|------|-----|
| 编号 | alg00010 |
| 对应 Subfeature | FT0002-B |

AiChatBus 收到 `AiChatConfigReq` 后的 Bus Context 初始化流程。

> 背景：编排器在 `TaskConfigReq` 阶段（alg00005 §4）对每个 BusTask 逐条发送 `AiChatConfigReq`，委托 AiChatBus 初始化对应 Bus Context（ADR-0009 消息委托，编排器不直接写 Context）。Bus Context 由 SessionMgr 在 GTID 分配时创建（alg00007），本流程只负责填充字段。

## 1. 入口

`AiChatConfigReq` 由编排器**逐条**发送（每条一个 BusTask，`head.busTaskIds` 为单元素列表），经 Router 按 `busTaskIds` 路由到达：

| 字段 | 内容 |
|------|------|
| `head.busTaskIds.at(0)` | 本 BusTask 的 GTID |
| `aiIndex` | AI 身份编号（0~7 参辩 / `kJudgeIndex` 裁判） |
| `systemPrompt` | 该 AI 的完整 system prompt（已含工作流说明与编号，编排器拼接） |
| `payload` | JSON：`apiUrl`、`apiKey`、`model`、`temperature` |

## 2. 校验

1. 按 GTID 从 TaskPool 获取 Context；不存在（GTID 无效/未创建）→ `AiChatConfigResp{isSuccess=false}`。
2. `aiIndex` 非法（大于 7 且不等于 `kJudgeIndex`）→ `AiChatConfigResp{isSuccess=false}`。
3. 解析 `payload` JSON；解析失败或缺少 `apiUrl`/`apiKey`/`model`/`temperature` 任一字段 → `AiChatConfigResp{isSuccess=false}`。

## 3. 初始化 Context

校验通过后将配置写入 `AiChatContext`：

| Context 字段 | 来源 |
|--------------|------|
| `modelName` | `payload.model` |
| `apiUrl` | `payload.apiUrl` |
| `apiKey` | `payload.apiKey` |
| `temperature` | `payload.temperature` |
| `aiIndex` | `req.aiIndex` |
| `systemPrompt` | `req.systemPrompt` |

## 4. 回复

`AiChatConfigResp{head, isSuccess=true}`，发送给 SessionDispatcher。

## 5. Boundary Conditions

| 输入 | 行为 |
|------|------|
| GTID 无对应 Context | `AiChatConfigResp{isSuccess=false}` |
| `aiIndex` 越界 | `AiChatConfigResp{isSuccess=false}` |
| payload 解析失败 / 缺字段 | `AiChatConfigResp{isSuccess=false}` |
| 重复配置（同 GTID 再次收到） | 覆盖写入（幂等） |
| 正常 | 写入 Context → `isSuccess=true` |

---

## 引用

- 消息：`AiChatConfigReq` / `AiChatConfigResp`（`docs/construct/messages.md`）
- 委托机制：ADR-0009（跨层 Context 消息委托）
- Context 字段：`docs/construct/contexts.md`（`AiChatContext`）
- 上游发送方：`alg00005` §4/§5（配置分发与确认）
- Context 创建：`alg00007`（BusTask 分配）
