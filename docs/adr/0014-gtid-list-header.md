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

## 实现

### MsgHead — 共享消息头 struct

消息头定义为 `common/message/MsgHead.mt`：

```mt
struct MsgHead
    vector<uint16_t> gtidList
    actor sourceAddress
```

生成 `MsgHead.hpp`：

```cpp
struct MsgHead {
    std::vector<uint16_t> gtidList;
    fw::EoAddress sourceAddress;
};
```

### 使用方法：显式声明 head 字段

每个需要路由/回复的消息通过 `include` 引入 MsgHead，并**显式声明** `MsgHead head` 字段：

```mt
include message/MsgHead.mt

message AiChatServiceReq
    MsgHead head
    string messagesJson
    string modelName
    double temperature
```

生成的 C++ 结构体——header 字段统一包裹在 `head` 下：

```cpp
struct AiChatServiceReq {
    MsgHead head;                    // gtidList + sourceAddress
    std::string messagesJson;        // 自身业务字段
    std::string modelName;
    double temperature;
};
```

访问方式：`req.head.gtidList`、`req.head.sourceAddress`。

### gen_code.py 扩展

- **`include` 行为不变**：仅添加 `#include "generated/message/MsgHead.hpp"` 和拉入 `define` 声明，**不做字段自动展开**
- **`vector` 类型支持**：`TYPE_MAP` 新增 `"vector" → ("std::vector", "<vector>")`
- **`Type<Args>` 模板语法**：字段类型和 `define` 均支持 `vector<uint16_t>` 形式
- **Bug 修复**：type 文件的 `define` 结果同步写入全局 `TYPE_MAP`

### 已迁移的消息

| 消息 | 变更 |
|------|------|
| `AiChatReq` | `include MsgHead` + `MsgHead head`，移除 `uint16 gtid` 和 `actor replyTo` |
| `AiChatResp` | `include MsgHead` + `MsgHead head`，移除 `uint16 gtid` |
| `AiChatServiceReq` | `include MsgHead` + `MsgHead head` |
| `AiChatServiceResp` | `include MsgHead` + `MsgHead head` |
| `SessionCloseReq` | `include MsgHead` + `MsgHead head`，移除 `uint16 gtid` |
| `SessionSetupResp` | `include MsgHead` + `MsgHead head`，移除 `uint16 gtid` |

业务代码对应变更：`.gtid` → `.head.gtidList[0]`，`.replyTo` → `.head.sourceAddress`。

### AiApiAdapter 回复路径修正

`AiApiAdapter::handle(AiChatServiceReq)` 原先通过 `this->senderAddress()` 原路返回。修改后从消息头提取 `req.head.sourceAddress`：

```
之前：AiApiAdapter ──resp──▶ ServiceGateway（原路返回）
之后：AiApiAdapter ──resp──▶ Router（按 head.sourceAddress 路由）
```

消息流转路径：

```
AiChatBus ──(head.sourceAddress=Router)──▶ ServiceGateway ──(透传 head)──▶ AiApiAdapter
                                                                               │
                                                                resp ──▶ Router ──▶ AiChatBus
```

`AiChatBus` 构造 `AiChatServiceReq` 时填充 `head.sourceAddress = routerAddr_`（Router 地址通过 `ModifyReq` 注入）。

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

- **消息体**：`gtid` 和 `fanOutGtids` 合并为 `gtidList`。消息头通过 `include message/MsgHead.mt` 以共享 struct 方式注入，消除每个消息重复声明。`payload` 字段从消息头中移除（payload 本就是消息体自身的业务字段，不需要在头中独立声明）
- **Router**：从"提取单个 GTID 路由"改为"遍历 gtidList 逐条路由"。行为更简单。当前阶段仅取 `gtidList[0]`，fan-out 遍历留待后续
- **Gateway**：fan-out 嵌入从"填 fanOutGtids 字段"改为"往 gtidList 里追加一项"
- **AiApiAdapter**：回复目标从 `senderAddress()`（原路返回）改为 `req.sourceAddress`（按消息头指定地址），使回复可经 Router 回到 AiChatBus 而非直接回 ServiceGateway
- **AiChatBus**：发送 `AiChatServiceReq` 时填充 `sourceAddress = routerAddr_`（Router 地址通过 `ModifyReq` 注入），确保 AiApiAdapter 的回复经 Router 路由
- **Adapter**：透传 gtidList，无需关心长度
- **SessionData**：包装消息时填 `gtidList: [gtid]`（单元素列表）
- **gen_code.py**：新增 `vector` 类型、`Type<Args>` 模板语法、`include` 字段注入机制
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
| 2026-06-20 | 实现：MsgHead struct + 显式 `MsgHead head` 成员（非字段注入）；gen_code.py 扩展（vector/Type\<Args\>）；6 个消息迁移；AiApiAdapter 回复路径修正为 req.head.sourceAddress |
