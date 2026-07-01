# Architecture Decision Records

本目录记录 Flow Hub 的关键架构决策。

## 格式

每个决策一个文件，文件名格式：`NNNN-slug.md`。采用 [Michael Nygard 的 ADR 格式](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions)。

## 决策列表

| 编号 | 标题 | 状态 |
|------|------|------|
| [0008](./0008-gtid.md) | 任务标识采用 GTID（General Task Identifier） | 已采纳 |
| [0009](./0009-gtid-context-rules.md) | GTID Context 访问规则、物理存储与映射表同步协议 | 已采纳 |
| [0010](./0010-eo-context-type.md) | EO 强制声明 ContextType 模板参数 | 已采纳 |
| [0011](./0011-gtid-routing-key.md) | GTID 替代虚拟 ID 作为路由键，Router 定位为层内设施 | 已采纳 |
| [0012](./0012-remove-protocol-gateway.md) | 取消 ProtocolGateway，统一为 ServiceGateway | 已采纳 |
| [0013](./0013-fan-out-gateway-embed.md) | fan-out 实现机制——Gateway 出向预埋 GTID 列表 | 已采纳 |
| [0014](./0014-gtid-list-header.md) | 消息头统一为 gtidList | 已采纳 |
| [0015](./0015-ai-chat-context-message.md) | AI Chat Context 消息格式 | 已采纳 |
| [0016](./0016-eo-env-wrapper.md) | 引入 EoEnv 包装层，彻底隐藏 CAF | 已采纳 |
| [0017](./0017-mayblock-compile-time-tag.md) | 编译期标签 `kMayBlock` 自动选择 Actor 线程模式 | 已采纳 |
| [0018](./0018-eo-zero-copy-delegate.md) | EoBase 消息转发零拷贝优化（onMsg move-aware + delegateTo const 拦截） | 已采纳 |
| [0019](./0019-router-route-table.md) | Router 路由表实现——定长数组 + Config/Reconfig 协议 + 混合转发策略 | 已采纳 |
| [0020](./0020-seq-version-control.md) | AiChatBus 序列号版本控制（抢占式请求） | 已采纳 |
| [0021](./0021-session-flags-compile-time.md) | SessionFlags 编译期 flag 映射模式 | 已采纳 |
| [0022](./0022-batch-fanout-two-level.md) | BatchFanOut/FanOutMsg 两级上行广播机制 | 已采纳 |
| [0023](./0023-bundled-request-gtid-sentinel.md) | 捆绑请求 + GTID 哨兵值新建 Task 协议 | 已采纳 |
| [0024](./0024-head-accesstype-reuse.md) | head.accessType 复用替代 sourceAddress 字段 | 已采纳 |

## 模板

新建 ADR 时复制以下模板：


# ADR-NNNN：决策标题

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 提议 | YYYY-MM-DD | — |

---

## 背景

## 决策

## 备选方案

| 方案 | 否决原因 |
|------|----------|

## 影响

