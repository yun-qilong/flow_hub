# alg00002：GTID→Adapter 映射

| 属性 | 值 |
|------|-----|
| 编号 | alg00002 |
| 对应 Subfeature | FT0002-A |

---

## 一、背景

业务响应从 Session 层回传时，Gateway 需将其路由到正确的 Adapter。GTID 是每个 Task 的唯一标识——Gateway 利用此性质建立映射表，无需消息头额外携带 Adapter 标识。

---

## 二、数据结构

`AccessGateway` 维护一个静态数组作为映射表：

| 属性 | 说明 |
|------|------|
| 数组大小 | 所有 Session-task GTID 的总数，即 `0x7000~0x7FFF`，共 `4096` 个 |
| 元素类型 | Adapter 的 `EoAddress`，空地址表示无映射 |
| 索引方式 | `gtid & 0x0FFF`——GTID 低 12 位直接作为数组下标 |

转换关系：

- GTID 低 12 位 = `(TaskType << 6) | Index`
- 每个 TaskType 占据数组中连续 64 个槽位
- 不同 TaskType 之间无重叠——TaskType 域已在 GTID 中编码 |

---

## 三、写入

### 3.1 Cookie

`TaskCreateReq` 和 `TaskCreateResp` 均携带 `cookie`（`TaskCreateCookie`）。Gateway 转发 `TaskCreateReq` 时将来源 Adapter 地址填入；SessionMgr 原样拷贝到 `TaskCreateResp`。Gateway 收到响应后从 `cookie` 读取目标地址。

### 3.2 时机

Gateway 收到 `TaskCreateResp{gtids, isSuccess=true, cookie}`：

1. 从 `cookie` 提取 Adapter 地址——若为空则打印 `ERR` 日志并丢弃
2. 对 `gtids` 中每个 GTID 写入：`map[gtid] = adapter`
3. 将 `TaskCreateResp` 转发给该 Adapter

### 3.3 失败

`isSuccess=false` 时不写映射表，仅转发失败响应给 Adapter。

---

## 四、查询

Gateway 收到 `AiChatBusinessResp`，以 `head.gtid` 查映射表：

- 命中 → 转发给对应 Adapter
- 未命中 → 打印 `ERR` 日志，丢弃消息

---

## 五、删除

Gateway 收到 `TaskDeleteReq{gtids}`，在转发给 SessionMgr 前：

1. 对 `gtids` 中每个 GTID 删除映射条目
2. 转发给 SessionMgr

无响应消息，不等待确认。

---

## 六、并发

Gateway 是单线程 actor。映射表所有操作在消息循环中串行执行。

---

## 七、边界条件

| 输入 | 行为 |
|------|------|
| `cookie` 为空 | 打印 `ERR`，丢弃 |
| `isSuccess=false` | 不写映射表，转发失败响应 |
| 查询时 `gtid` 不在映射表中 | 打印 `ERR`，丢弃 |
| 同一 `gtid` 重复写入 | 覆盖 |
| 删除时 `gtid` 不在映射表中 | 无操作 |
