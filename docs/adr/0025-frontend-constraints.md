# ADR-0025：前端行为约束汇总

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-02 | 韵启龙 |

---

## 背景

接入层与会话层设计中，大量行为约束散落在 checklist 和各 ADR 中。其中涉及前端的约束需要集中记录，作为前端实现和协议设计的硬性边界。

---

## 决策

### 1. 连接约束

- **一个连接对应一个 AppType**：前端声明 AppType，一个连接只使用一种 AppType。
  - 依据：`access-session-design-checklist.md` Round 1 §1.1
- **同一 Adapter 内一个 user 最多一个连接**：不允许多开。Adapter 内使用 `userToConn_[userId]`，单连接使查表退化为 O(1)。
  - 依据：`access-session-design-checklist.md` Round 1 §1.6
- **同一连接不允许同时发起多个注册请求**：注册期间的匹配依赖 `connectionId`，并发注册将无法区分响应归属，后端不保证并发注册的响应顺序。
  - 依据：本 ADR（实现阶段决策）

### 2. 协议约束

- **注册与登录分离**：注册只分配 `userId`，不建立连接状态。前端注册成功后需另行发起登录。
  - 依据：`access-session-design-checklist.md` Round 7 §7.1.3
- **username 最长 12 字符**：`kMaxUsernameLen = 12`。超长注册请求后端直接拒绝。利于固定大小数据结构组织。
  - 依据：本 ADR（2026-07-04 修订）
- **username 是 userId 级别的**：注册只建立 `username ↔ userId` 映射，与 appType 无关。注册成功后该 userId 自动获得所有 appType 的登录权限。
  - 依据：`access-session-design-checklist.md` Round 7 §7.1（修订）
- **不存在独立的"建空 session"请求**：新建会话总是伴随第一条数据消息（捆绑请求）。前端填入哨兵 GTID（`taskType` 位如实，序列号位全 1）。
  - 依据：`access-session-design-checklist.md` Round 8 §8.1、ADR-0023
- **收到 ACK 即确认会话创建成功 + 消息已处理**：一条 ACK 完成两项确认，不需要额外的新会话创建确认。
  - 依据：ADR-0023

### 3. 前端不感知的类型

- **前端不感知 `AccessType`**：由 Adapter 编译期填入。
- **前端不发送 `appType`**：由 Adapter 编译期填入 `UserHead`。前端无需关心自己是哪种 App。
  - 依据：`access-session-design-checklist.md` Round 1 §1.2 类型可见性矩阵

### 4. 超时责任

- **上下文同步超时由前端负责**：FlowHub 无定时器机制，无法主动检测超时。前端自行维护超时计时。
  - 依据：`access-session-design-checklist.md` Round 9 §9.2.1
- **BatchCounter 资源防枯竭由后端负责**：分配时检查超时 counter 并 GC。

### 5. ACK 与消息同步语义

- **发送方前端**：收到 `head.accessType == 自身 accessType` → "消息已送达"确认
- **其他端前端**：收到 `head.accessType != 自身 accessType` → "新消息同步"通知，实时显示
  - 依据：`access-session-design-checklist.md` Round 6 §6.4.2、ADR-0020

### 6. 错误处理

- 错误沿请求的反向路径返回：`BusinessEO → Router → SessionData → Gateway → Adapter → 前端`
- 若 `needsAck` 为 true 但处理失败，BusinessEO 不发 ACK（ACK 仅表示成功），错误单独经错误路径返回。
  - 依据：`access-session-design-checklist.md` Round 9 §9.3

### 7. 未声明新 Task 又无 GTID

- 前端发送无 GTID 的消息且非哨兵值时，Router 返回 `NO_GTID_NEW_TASK` 错误。
  - 依据：ADR-0023

---

## 注册消息的 connectionId 约定

注册时 `head.uid` 为 `kInvalidUid`（`0xFFFF`），无法通过 `uid` 区分并发注册请求。因此：

- `UserRegisterReq` 和 `UserRegisterResp` 的消息体中携带 `uint8_t connectionId`
- `connectionId` 由 Adapter 在发出注册请求时分配（如递增序号或连接句柄），响应中原样回传
- Adapter 凭 `connectionId` 匹配响应到对应连接
- 同一连接不允许同时发起多个注册请求（见 §1）

---

## 影响

- 前端实现需遵守以上所有硬性约束
- 注册消息需新增 `connectionId` 字段
- `kInvalidUid = 0xFFFF` 常量需在 `Constants.hpp` 中定义
