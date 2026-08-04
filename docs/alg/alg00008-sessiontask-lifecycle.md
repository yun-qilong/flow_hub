# alg00008：SessionTask Lifecycle

| 属性 | 值 |
|------|-----|
| 编号 | alg00008 |

---

SessionTask 的创建与删除流程。

## 1. Creation

前端经 AccessGateway 向 SessionMgr 发送 `TaskCreateReq`：

- `head.sessionTaskId` = `kInvalidGtid`
- `head.busTaskIds` = []
- `taskType` 和 `cookie` 按需填写

SessionMgr 分配一个 SessionTask GTID 及对应 Context，通过 `TaskCreateResp` 返回：

- `head.sessionTaskId` = 新分配的 GTID
- `isSuccess`：`true` / `false`
- `cookie` 原样拷贝

AccessGateway 在 `TaskCreateResp` 返回时写入 GTID→Adapter 映射（详见 alg00002）。

## 2. Deletion

### 2.1 入口

前端经 AccessGateway 发送 `TaskDeleteReq`。AccessGateway 按 `head.sessionTaskId` 的 `TaskType` 路由到对应编排器（经 SessionDispatcher）。此时不清理映射。

### 2.2 编排器清理

编排器收到后做必要的资源清理（如下挂 BusTask），然后将 `TaskDeleteReq` 转发给 SessionMgr 回收 SessionTask GTID。不期待 SessionMgr 应答。

转发后编排器直接向前端回复 `TaskDeleteResp`：

- `head.sessionTaskId` = 已删除的 GTID
- `isSuccess` = `true`

### 2.3 Gateway 收尾

AccessGateway 收到 `TaskDeleteResp` 后，清除 `gtidToAdapter_` 中对应映射条目，然后转发给前端。
