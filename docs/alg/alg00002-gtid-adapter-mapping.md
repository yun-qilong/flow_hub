# alg00002：AccessGateway Routing

| 属性 | 值 |
|------|-----|
| 编号 | alg00002 |
| 对应 Subfeature | FT0002-A, FT0002-B |

---

## 1. Background

AccessGateway 是 Access 层的消息枢纽，承担两个方向的寻址：

1. **下行**（Adapter → Session 层）：按消息类型分发——C 面消息（`TaskCreateReq`）转发给 `SessionMgr`，D 面消息（其余，含 `TaskDeleteReq`）转发给 `SessionDispatcher`。
2. **上行**（Session 层 → Adapter）：使用 GTID→Adapter 映射表将回复路由回正确的 Adapter。

---

## 2. Downlink Routing

| 消息类型 | 分类 | 转发目标 |
|------|:--:|------|
| `TaskCreateReq` | C 面 | SessionMgr |
| `TaskDeleteReq` | D 面 | SessionDispatcher |
| `TaskConfigReq` | D 面 | SessionDispatcher |
| `AiAgoraChatReq` | D 面 | SessionDispatcher |
| `AiAgoraResetReq` | D 面 | SessionDispatcher |
| 其他 D 面消息 | D 面 | SessionDispatcher |

SessionMgr 和 SessionDispatcher 的地址通过配置注入。

---

## 3. Uplink Routing

### 3.1 主路径：gtidToAdapter_ 映射表

上行消息携带 `head.sessionTaskId`，AccessGateway 据此查找目标 Adapter。

Session-task GTID 编码为 16-bit，其中低 12 位由 `(TaskType << 6) | Index` 构成。Session-task 的 GTID 范围为 `0x7000~0x7FFF`，恰好 4096 个。因此一个 4096 槽位的数组即可覆盖全部：

```
index = gtid & 0x0FFF
gtid  = 0x7000 | index
```

`gtidToAdapter_` 为 `EoAddress` 定长数组，以 `index` 为下标。空地址表示无映射。

AccessGateway 收到任何携带 `head.sessionTaskId` 的 Session 层回复时，查表转发。

### 3.2 映射表写入：TaskCreate Cookie

`TaskCreateReq` 到达时尚未分配 GTID，`sessionTaskId` 为 `kInvalidGtid`，无法查映射表。AccessGateway 在此消息的 `cookie.adapterAddr` 中填入来源 Adapter 的地址，随消息一路携带到 SessionMgr。

`TaskCreateResp` 返回时携带同一个 `cookie`，新分配的 GTID 位于 `head.sessionTaskId`。AccessGateway 处理流程：

1. 若 `isSuccess=false`：直接转发 `TaskCreateResp` 给 `cookie.adapterAddr`。不写映射表。
2. 若 `isSuccess=true`：写入 `gtidToAdapter_[head.sessionTaskId & 0x0FFF] = cookie.adapterAddr`。转发给 Adapter。

### 3.3 删除

AccessGateway 收到 `TaskDeleteResp` 时，清除 `head.sessionTaskId` 对应的 `gtidToAdapter_` 映射条目，然后转发给前端。

---

## 4. Boundary Conditions

| 输入 | 行为 |
|------|------|
| 未知消息类型 | 日志告警，丢弃 |
| `cookie.adapterAddr` 为空 | Session 层发起，跳过写入 |
| `isSuccess=false` | 不写映射表 |
| 查询未命中 | 日志告警，丢弃 |
| 重复写入 | 覆盖 |
| 删除时不存在 | 无操作 |
