# alg00012：CliAdapter（单 AI 前端）

| 属性 | 值 |
|------|-----|
| 编号 | alg00012 |
| 对应 Subfeature | FT0002-B |

CLI 前端交互逻辑。**仅支持单 AI chat**（`aiCount=1`、`hasJudge=false`，无裁判/辩论）——多 AI 辩论为 `-D` scope，本设计不涉及。

## 1. 状态机

| 状态 | 提示符 | 说明 |
|------|--------|------|
| `haveNotGtid` | `> ` | 无会话 |
| `hasGtid` | `[0x<gtid>]> ` | 会话中（config 已完成） |

等待回复期间（`waiting`）输入被忽略，提示符显示 `... `。

## 2. 命令

| 命令 | 状态 | 行为 |
|------|------|------|
| `/chat` | `haveNotGtid` | 创建会话：TaskCreate + TaskConfig 流程（§3） |
| 普通文字 | `hasGtid` | 发 `AiAgoraChatReq`（§4） |
| `/reset` | `hasGtid` | 发 `AiAgoraResetReq`（§4） |
| `/quit` | `hasGtid` | 发 `TaskDeleteReq`，回 `haveNotGtid`（§5） |
| `/help` | 任意 | 显示命令帮助 |
| `/exit` | 任意 | 退出程序 |

## 3. `/chat`：创建会话

1. 发 `TaskCreateReq{taskType=AiAgora}`（`head.gtidList` 为空，携带 `cookie`）。
2. 收 `TaskCreateResp`：
   - `isSuccess=false` → 提示失败，回 `haveNotGtid`。
   - `isSuccess=true` → 记录 `head.sessionTaskId`（Session GTID）。
3. 发 `TaskConfigReq`（单 AI payload，见 §3.1）。
4. 收 `TaskConfigResp`：
   - `isSuccess=true` → 进入 `hasGtid`，展示 GTID。
   - `isSuccess=false` → 提示失败，回 `haveNotGtid`（GTID 已分配，可用 `/quit` 清理）。

### 3.1 TaskConfigReq payload（单 AI）

按 `-B` 约束固定：`aiCount=1`、`hasJudge=false`，`configs` 长度 1。

| 字段 | 值 |
|------|-----|
| `aiCount` | `1` |
| `hasJudge` | `false` |
| `maxRounds` | 默认值（实现时定） |
| `maxResponseLength` | 默认值（实现时定） |
| `timeoutMs` | 默认值（实现时定） |
| `configs[0]` | `apiUrl` / `apiKey` / `model` / `systemPrompt` / `temperature` |

> `configs[0]` 参数来源：CLI 无配置 UI，由启动参数/配置文件在 main 构造时提供（实现时定）。

## 4. 会话中交互

- **文字输入** → 发 `AiAgoraChatReq{content}`。收到 `AiAgoraChatResp` 后展示 `responses`（单 AI 为原始回答，无前缀），直到 `isComplete=true` 或 `errorCode≠0`（单 AI 模式通常一次完成）。
- **`/reset`** → 发 `AiAgoraResetReq` → 收 `AiAgoraResetResp` → 展示 `estimatedTopicCount`。

## 5. `/quit`：删除会话

发 `TaskDeleteReq{head.sessionTaskId=当前 GTID}` → 回 `haveNotGtid`。不阻塞等待响应（`TaskDeleteResp` 到达仅作日志）。

## 6. 消息收发

CLI 发出的 `*Req` 经 AccessGateway 路由（`TaskCreateReq` → SessionMgr；其余 D 面消息 → SessionDispatcher）；收到的 `*Resp` 由 AccessGateway 按 GTID→Adapter 映射回投（alg00002）。CLI 只面向 AccessGateway，不感知内部路径。

## 引用

- 协议：`docs/feature/FT0002/frontend-protocol.md`（单 AI 会话生命周期）
- 消息：`docs/construct/messages.md`
- 路由：`alg00002`（AccessGateway）
- 单 AI 编排：`alg00006`（单 AI 模式）
