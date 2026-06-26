# ADR-0020：消息 seq 版本控制与跨设备同步

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-26 | 韵启龙 |

---

## 背景

在 ADR-0015 中确定了 AiChat 对话历史以 JSON 拼接方式存储在 Context，由 `AiChatStage` 枚举（`AwaitingUser` / `AwaitingServiceResp`）管理状态。

后续接入层与会话层设计（access-session-design-checklist）引入了多设备同步、跨窗口消息广播等需求，暴露了原有设计的不足：

1. **消息顺序无权威来源**：前端无法得知消息在对话中的确切位置
2. **并发请求处理粗糙**：等待 AI 回复期间收到新用户消息直接丢弃，多设备场景下用户体验差
3. **缺乏确认机制**：前端发消息后无从得知是否已被后端处理
4. **消息头缺乏接入来源信息**：无法追踪请求的 accessType 和 clientId

---

## 决策

### 1. 消息序列号（seq）

- **seq 由 AiChatBus 在写入 context 时分配**，0-based，严格单调递增
- seq 同时作为 `messageOffsets` 数组的直接下标
- context 新增 `uint16_t messageOffsets[256]`：每条消息在 `messagesBuffer` 中的起始字节偏移
- context 新增 `uint8_t messageCount`：当前消息总数
- seq=0 对应首条用户消息（system prompt 不占 seq，始终在 offset 0）

### 2. 版本检测替代状态机

- 废弃 `AiChatStage` 枚举，引入 `uint16_t pendingReqSeq`
  - `pendingReqSeq == 0`：空闲（等价于原 `AwaitingUser`）
  - `pendingReqSeq != 0`：等待回复（等价于原 `AwaitingServiceResp`）
- `AiChatServiceReq` 和 `AiChatServiceResp` 新增 `uint16_t reqSeq` 字段
- AiChatBus 发送请求时填入 `reqSeq = pendingReqSeq`
- AiApiAdapter 原样回传 reqSeq
- AiChatBus 收到回复时比对：`resp.reqSeq == ctx.pendingReqSeq` 判定有效

### 3. 抢占式请求

等待 AI 回复期间收到新用户消息时：

1. 立即分配 seq、写入 context、发送 `AiChatMsgAck`
2. 更新 `pendingReqSeq` 为新版本号
3. **立即发起新的 API 请求**（携带更新后的 reqSeq）
4. 旧 API 回复到达时因 reqSeq 不匹配被直接丢弃

不主动取消旧请求（HTTP 无法可靠中断），旧回复的 API 费用视为可接受的浪费。

### 4. AiChatMsgAck 确认消息

新增消息类型，AiChatBus 在每次用户消息写入 context 后立即发送：

```
message AiChatMsgAck
    UserHead head       # gtidList + userInfo
    uint16 seq          # 分配的序列号
    string content      # 原消息内容（发送方据此匹配）
```

用途：发送方前端标记"已送达"；其他前端通过 SessionData fan-out 机制接收后实时显示。

### 5. 消息头重构

- `MsgHead` 更名为 `UserHead`，表明这是用户请求携带的头部
- 新增 `UserInfo` 结构体嵌入 `UserHead`：

```
struct UserInfo
    uint8 userId        # 用户标识
    uint8 accessType    # 接入方式（CLI/Web/Desktop）
    uint8 clientId      # 客户端连接标识

struct UserHead
    vector<GTID> gtidList
    EoAddress sourceAddress    # 待废弃（前置共识已定）
    UserInfo userInfo
```

---

## 影响

- **AiChatContext**：新增 `messageOffsets[256]`、`messageCount`、`pendingReqSeq`；移除 `stage`、`turnCount`
- **AiChatBus**：新增 `allocateAndRecordSeq()`、`sendAck()` 方法；`processServiceRequest` 支持抢占；`processBusinessResp` 增加 seq 版本校验
- **AiChatServiceReq/Resp**：各增加 `uint16_t reqSeq` 字段
- **消息类型**：`MsgHead` → `UserHead` 更名；新增 `UserInfo`、`AiChatMsgAck`；所有引用消息头定义的 .mt 文件均需更新
- **AiApiAdapter**：需透传 `reqSeq`（请求存入 → 响应原样回填）
- **生成代码**：删除 `AiChatStage.hpp`，新增 `UserInfo.hpp`、`UserHead.hpp`、`AiChatMsgAck.hpp`

---

## 与现有 ADR 的关系

- ADR-0015：Context 结构和消息流转逻辑以此 ADR 为准更新
- ADR-0014：消息头 `gtidList` 不变，类型名从 `MsgHead` 变更为 `UserHead`
- ADR-0008/0011：GTID 编解码和路由键不受影响

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-26 | 初稿，采纳 |
