# 消息结构定义

> 按新增顺序罗列。每消息一章。

---

## UserHead

所有 D 面业务消息携带此头。

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `gtid` | `GTID` | ✅ | 当前消息关联的 GTID |
| `fanoutList` | `vector<GTID>` | — | 多 GTID 场景，为空表示无 fan-out |

---

## TaskCreateReq

方向：Access/Session → SessionMgr，经 Gateway 转发。

| 字段 | 类型 | 必填 | 填写者 | 说明 |
|------|------|------|--------|------|
| `taskType` | `TaskType` | ✅ | 请求方 | 期望创建的 Task 类型 |
| `requestNum` | `uint8_t` | ✅ | 请求方 | 申请 GTID 数量，≥ 1 |
| `cookie` | `TaskCreateCookie` | ✅ | Gateway | 路由信息。编排器发起时为空 |

---

## TaskCreateResp

方向：SessionMgr → Gateway。

| 字段 | 类型 | 必填 | 填写者 | 说明 |
|------|------|------|--------|------|
| `gtids` | `vector<GTID>` | ✅ | SessionMgr | 分配的 GTID 列表，长度 = `requestNum` |
| `isSuccess` | `bool` | ✅ | SessionMgr | `true` = 成功；`false` = 空闲不足或 `requestNum=0` |
| `cookie` | `TaskCreateCookie` | ✅ | SessionMgr | 从 `TaskCreateReq.cookie` 原样拷贝 |

---

## TaskDeleteReq

方向：Access/Session → SessionMgr，经 Gateway 转发。无响应。

| 字段 | 类型 | 必填 | 填写者 | 说明 |
|------|------|------|--------|------|
| `gtids` | `vector<GTID>` | ✅ | 请求方 | 要回收的 GTID 列表 |

---

## TaskCreateCookie

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `adapterAddr` | `EoAddress` | — | 非空 = Access 层发起；空 = Session 层发起 |
