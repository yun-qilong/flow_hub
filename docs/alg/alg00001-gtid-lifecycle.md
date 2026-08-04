# alg00001：GTID 生命周期管理

| 属性 | 值 |
|------|-----|
| 编号 | alg00001 |
| 对应 Subfeature | FT0002-A |

---

## 1. Background

GTID 是 16-bit 无符号整数，格式如下（ADR-0008）：

```
15        12  11        6  5        0
┌────────────┬───────────┬───────────┐
│  Category  │ TaskType  │   Index   │
│  (4 bits)  │ (6 bits)  │ (6 bits)  │
└────────────┴───────────┴───────────┘
```

`TaskType` 域（bit[11:6]）标识任务类型，`Index` 域（bit[5:0]）为同类型下的实例编号，最多 64 个。

SessionMgr 是 GTID 分配与回收的唯一入口。SessionTask 逐个分配（`TaskCreateReq`），BusTask 批量分配（`BusTaskCreateReq`）。回收同理。

---

## 2. Pool Model

按 `TaskType` 分池。每池管理 64 个槽位（对应 Index 域 0~63），标记占用/空闲。

GTID 合成：`gtid = (category << 12) | (taskType << 6) | index`

### 2.1 递增分配

分配策略为**递增轮转**：每池维护一个 `nextIndex` 游标，从 0 起始。每次分配时从游标位置向后查找第一个空闲槽位，分配后游标推进到该位置 +1。游标达 63 后回绕到 0。

此策略避免刚回收的 GTID 被立即重新分配。若存在超时未清理的旧任务仍持有该 GTID，同时新 task 分配到同一 GTID，将导致新旧两个 task 同时操作同一 Context。

> 相关修复：RI0003 (#22)

---

## 3. Messages

| 消息 | 用途 |
|------|------|
| `TaskCreateReq` / `TaskCreateResp` | 申请单个 SessionTask GTID |
| `BusTaskCreateReq` / `BusTaskCreateResp` | 批量申请 BusTask GTID |
| `TaskDeleteReq` | 回收单个 SessionTask GTID（无直接响应） |
| `BusTaskDeleteReq` / `BusTaskDeleteResp` | 批量回收 BusTask GTID |

---

## 4. Allocation

### 4.1 SessionTask（`TaskCreateReq`）

单个分配。SessionMgr 检查 `taskType` 对应池有空闲槽位则分配一个 index，合成 GTID 填入 `TaskCreateResp.head.sessionTaskId`。无空闲则 `isSuccess=false`。

### 4.2 BusTask（`BusTaskCreateReq`）

批量分配。SessionMgr 遍历 `taskTypes`，逐个分配。

**原子性约束**：若任一个分配失败（空闲不足），已分配的 GTID 全部回收，回复 `BusTaskCreateResp{isSuccess=false}`。全部成功则 `isSuccess=true`，`head.busTaskIds` 与 `taskTypes` 一一对应。

---

## 5. Deallocation

### 5.1 SessionTask（`TaskDeleteReq`）

SessionMgr 收到后提取 `head.sessionTaskId` 的 `taskType` 和 `index`，标记槽位为空闲，回收 Context。无直接响应——编排器负责回复前端 `TaskDeleteResp`（详见 alg00008）。

### 5.2 BusTask（`BusTaskDeleteReq`）

SessionMgr 遍历 `head.busTaskIds`，逐个回收槽位 Context。回复 `BusTaskDeleteResp{isSuccess}`。

重复回收无副作用。标记已空闲的位再次标记不产生错误。

---

## 6. Context Binding

- 分配 GTID 时，按 `TaskType` 推导 Context 类型，创建并初始化
- 回收 GTID 时，销毁 Context

---

## 7. Concurrency

SessionMgr 是单线程 actor。分配/回收在消息循环中串行执行。

---

## 8. Boundary Conditions

| 输入 | 行为 |
|------|------|
| `taskTypes` 长度超过空闲槽位数 | 返回 `isSuccess=false`，零分配 |
| `taskTypes` 为空 | 返回 `isSuccess=false` |
| 重复回收 | 无副作用 |
