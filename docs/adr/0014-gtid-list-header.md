# ADR-0014：消息头统一为 gtidList

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-16 | 韵启龙 |

---

## 背景

原消息头规范（ADR-0008、ADR-0011）定义消息携带三个字段：

| 字段 | 类型 |
|------|------|
| GTID | `uint16_t` |
| sourceAddress | `EoAddress` |
| payload | 业务数据 |

其中 GTID 为单个 `uint16_t`，fan-out 场景通过额外的独立字段 `fanOutGtids` 携带附加目标。

ADR-0013 确定了 fan-out 由 Gateway 出向预埋、Router 拆 list 的机制后，消息头中出现了两个 GTID 相关字段（`gtid` 和 `fanOutGtids`），Router 需要两条处理路径——先处理主 GTID，再检查 fan-out 列表。

---

## 决策

**将 `GTID` 和 `fanOutGtids` 合并为统一的 `gtidList`。消息头从三字段缩减为两字段。**

| 字段 | 类型 | 含义 |
|------|------|------|
| **gtidList** | `vector<uint16_t>` | 目标 GTID 列表。通常长度=1（普通消息），fan-out 时长度>1 |
| **sourceAddress** | `EoAddress` | 回复地址 |

**Router 行为统一**：

```cpp
// 单 GTID 和多 GTID 走同一条路径
for (auto gtid : msg.gtidList) {
    auto taskType = gtid >> 6;
    auto target = lookup(taskType);
    delegateTo(target, msg);
}
```

单 GTID 就是遍历一次，fan-out 就是遍历多次——逻辑完全统一。

---

## 为什么不是其他方案

### 方案 A：保留 GTID + 独立 fanOutGtids（被否决）

```
消息头：{gtid: uint16_t, fanOutGtids: vector<uint16_t>?, sourceAddress}
```

**否决理由**：
- Router 需要两条路径：先处理 gtid，再判断 fanOutGtids 是否存在
- 两个字段语义重叠，都表达"目标"
- Adapter（将来）透传时需维护两个字段

### 方案 B：gtidList 用定长数组（被否决）

```
gtidList: StaticVector<uint16_t, 8>  // 或 std::array
```

**否决理由**：当前阶段直接使用 `std::vector` 更灵活。Migration 到 `StaticPool` 或 `StaticVector` 等以符合静态内存池约束，留到内存模型实施阶段（ADR 后续）。

---

## 影响

- **消息体**：`gtid` 和 `fanOutGtids` 合并为 `gtidList`。`payload` 字段从消息头中移除（payload 本就是消息体自身的业务字段，不需要在头中独立声明）
- **Router**：从"提取单个 GTID 路由"改为"遍历 gtidList 逐条路由"。行为更简单
- **Gateway**：fan-out 嵌入从"填 fanOutGtids 字段"改为"往 gtidList 里追加一项"
- **Adapter**：透传 gtidList，无需关心长度
- **SessionData**：包装消息时填 `gtidList: [gtid]`（单元素列表）
- **文档**：§3.3、§6.2、§6.4 等章节省略"payload"头字段，消息头从三个字段简化为两个
- **GTID 概念不变**：GTID 作为统一任务标识的 16-bit 定义、Category/TaskType/Index 编码、ADR-0008 全部不变。仅消息头中从单个变为列表

---

## 与现有 ADR 的关系

- ADR-0008：GTID 位结构定义不变
- ADR-0011：`sourceAddress` 规则不变
- ADR-0013：fan-out 机制不变（Gateway 预埋 + Router 拆），只是嵌入方式从独立字段改为往 gtidList 追加

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-16 | 初稿，采纳 |
