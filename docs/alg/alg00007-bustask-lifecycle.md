# alg00007：BusTask Allocation & Deletion

| 属性 | 值 |
|------|-----|
| 编号 | alg00007 |


---

编排器通过 `BusTaskCreateReq` 向 SessionMgr 批量申请 BusTask GTID，通过 `BusTaskDeleteReq` 批量回收。

## 1. BusTask Allocation

### 1.1 发送请求

编排器构造 `BusTaskCreateReq`：

- `head.sessionTaskId` = 当前 SessionTask GTID
- `head.busTaskIds` = []（申请时为空）
- `taskTypes` = 所需各 BusTask 的类型列表，长度 ≥ 1，每个元素为有效 `TaskType` 枚举值。不同类型可混合申请

消息直发 SessionMgr，不经 SessionDispatcher 中转。SessionMgr 回复同样直通返回编排器。

### 1.2 SessionMgr 处理

SessionMgr 遍历 `taskTypes`，逐个分配 GTID。分配结果填入 `head.busTaskIds`，与 `taskTypes` 一一对应。

**原子性约束**：若任一个 GTID 分配失败（空闲不足），已分配的 GTID 全部回收，回复 `BusTaskCreateResp{isSuccess=false}`。全部成功则 `isSuccess=true`。

### 1.3 编排器接收

`isSuccess=true` 时，`head.busTaskIds` 中 GTID 顺序与 `taskTypes` 一一对应，由编排器自行分配用途。

`isSuccess=false` 时编排器执行上层回滚逻辑。

## 2. BusTask Deletion

编排器向 SessionMgr 发送 `BusTaskDeleteReq`：

- `head.sessionTaskId` = 当前 SessionTask GTID
- `head.busTaskIds` = 待删除的 BusTask GTID 列表

SessionMgr 回收这些 GTID 及对应 Context，回复 `BusTaskDeleteResp{isSuccess}`。消息直通，不经 Dispatcher。

编排器在删除 SessionTask 前，将所有下挂 BusTask GTID 一次性填入 `head.busTaskIds` 发送 `BusTaskDeleteReq` 完成清理。
