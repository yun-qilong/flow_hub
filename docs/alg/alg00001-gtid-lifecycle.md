# alg00001：GTID 生命周期管理

| 属性 | 值 |
|------|-----|
| 编号 | alg00001 |
| 对应 Subfeature | FT0002-A |

---

## 一、背景

GTID 是 16-bit 无符号整数，格式如下（ADR-0008）：

```
15        12  11        6  5        0
┌────────────┬───────────┬───────────┐
│  Category  │ TaskType  │   Index   │
│  (4 bits)  │ (6 bits)  │ (6 bits)  │
└────────────┴───────────┴───────────┘
```

`TaskType` 域（bit[11:6]）标识任务类型，`Index` 域（bit[5:0]）为同类型下的实例编号，最多 64 个。

SessionMgr 是 GTID 分配与回收的唯一入口。请求方可批量申请多个同 TaskType 的 GTID，也可批量回收。请求来源有两类：Access 层（前端经 Adapter）和 Session 层（编排器）。

---

## 二、池模型

按 `TaskType` 分池。每池管理 64 个槽位（对应 Index 域 0~63），标记占用/空闲。

GTID 合成：`gtid = (taskType << 6) | index`

---

## 三、消息定义

消息结构详见 messages 文档：

- [TaskCreateReq](../construct/messages.md#taskcreatereq)
- [TaskCreateResp](../construct/messages.md#taskcreateresp)
- [TaskDeleteReq](../construct/messages.md#taskdeletereq)


---

## 四、分配

### 4.1 原子校验

SessionMgr 收到 `TaskCreateReq` 后，先检查 `taskType` 对应池的剩余空闲槽位数 ≥ `requestNum`。不足则**一个都不分配**，返回 `isSuccess=false`。

### 4.2 分配步骤

1. 校验 `requestNum` ≤ 空闲槽位数，不满足则返回失败
2. 从池中取 `requestNum` 个空闲 index（最低空闲位优先）
3. 标记这些 index 为已占用
4. 按 `(taskType << 6) | index` 逐个合成 GTID，填入 `gtids`
5. 为每个分配的 GTID 创建 Context
6. 将 `cookie` 从请求原样拷贝到响应
7. 返回 `isSuccess=true`

---

## 五、回收

### 5.1 处理

SessionMgr 收到 `TaskDeleteReq`，对 `gtids` 中每个 GTID：

1. 提取 `taskType = gtid >> 6`，`index = gtid & 0x3F`
2. 在对应池中标记该 index 为空闲
3. 销毁该 GTID 的 Context

### 5.2 无响应

回收不产生响应消息。发送方不等待确认。

### 5.3 幂等

重复回收无副作用。标记已空闲的位再次标记不产生错误。

---

## 六、Context 绑定

- 分配 GTID 时，按 `TaskType` 推导 Context 类型，创建并初始化
- 回收 GTID 时，销毁 Context

---

## 七、并发

SessionMgr 是单线程 actor。分配/回收在消息循环中串行执行。

---

## 八、边界条件

| 输入 | 行为 |
|------|------|
| `requestNum` > 空闲槽位数 | 返回 `isSuccess=false`，打印 `WRN` 日志，零分配 |
| `requestNum` = 0 | 返回 `isSuccess=false`，打印 `ERR` 日志 |
| 回收时 `index` 超出 0~63 | 不做校验 |
| 重复回收 | 无副作用 |
