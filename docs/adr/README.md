# Architecture Decision Records

> 架构决策记录。共 24 篇，按主题分为 6 组。

---

## 一、代码生成（1 篇）

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0007](0007-code-generation.md) | 消息、类型、Context 统一代码生成 | `.mt` 定义文件 → Python 脚本 → 自动生成 C++ 代码，消除手写不一致 |

## 二、GTID 体系（3 篇）

GTID（General Task Identifier）是 Flow Hub 的统一任务标识，16-bit 三级编码。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0008](0008-gtid.md) | 任务标识采用 GTID | 16-bit：Category(4) + TaskType(6) + Index(6) |
| [0011](0011-gtid-routing-key.md) | GTID 替代虚拟 ID 作为路由键 | Router 直接用 `gtid >> 6` O(1) 路由，取消中间映射层 |
| [0014](0014-gtid-list-header.md) | 消息头统一为 gtidList | 单 GTID 和多 GTID（fan-out）走同一遍历路径 |

## 三、Context 与 EO（2 篇）

EO 无状态，所有状态在 Context 中，由 TaskPool 统一管理。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0009](0009-gtid-context-rules.md) | GTID Context 访问规则与存储 | Context 存在 TaskPool（静态内存池）；仅 Business D 面 EO 有读写权限；Cache line 隔离 |
| [0010](0010-eo-context-type.md) | EO 通过 TaskType 模板参数绑定 Context | `AiChatBus<TaskType::AiChat>` → `ContextTypeOf<T>` 编译期推导，零运行时开销 |

## 四、Fan-out 机制（2 篇）

Fan-out 分两种场景：下行（Service 层抄送多个 Business EO）和上行（Business EO 广播到所有 Access Adapter）。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0013](0013-fan-out-gateway-embed.md) | 下行 fan-out——Gateway 出向预埋 GTID | ServiceGateway 在出向请求中嵌入额外 GTID，Adapter 透传，Router 拆 list 路由 |
| [0022](0022-batch-fanout-two-level.md) | 上行 fan-out——BatchFanOut/FanOutMsg 两级广播 | SessionData 组装 BatchFanOut（含 targets 位图）→ Gateway 拆解为 FanOutMsg 逐 Adapter 发送 |

## 五、编译期设计（3 篇）

利用 C++17 编译期能力将错误从运行时前移到编译阶段。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0017](0017-mayblock-compile-time-tag.md) | `kMayBlock` 编译期标签 | EO 声明 `static constexpr bool kMayBlock` → `createEo` 自动选独立线程或共享池 |
| [0021](0021-session-flags-compile-time.md) | SessionFlags 编译期 flag 映射 | `SessionFlags::make<AppType>()` 编译期 switch → uint8_t 位掩码，运行时零开销 |
| [0023](0023-bundled-request-gtid-sentinel.md) | 哨兵 GTID 捆绑请求 | 序列号位全 1 = 哨兵值，AccessGateway 检测 → SessionMgr 分配正式 GTID，一条消息完成"建 Task + 首条数据" |

## 六、工程基础设施（7 篇）

框架封装、消息格式、路由协议。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0012](0012-remove-protocol-gateway.md) | 取消 ProtocolGateway，统一 ServiceGateway | 所有外部服务经统一 Gateway 进出，简化 Service 层 |
| [0015](0015-ai-chat-context-message.md) | AI Chat Context 与消息体设计 | 对话历史存 AiChatContext.messagesBuffer（16KB 静态缓冲区） |
| [0016](0016-eo-env-wrapper.md) | EoEnv 包装层隐藏 CAF | 业务代码零 `caf::` 引用，框架切换仅改 `fw/` |
| [0018](0018-eo-zero-copy-delegate.md) | 消息转发零拷贝 | `delegate()` 移动消息所有权，不复制 payload |
| [0019](0019-router-route-table.md) | Router 路由表 | 1024 项 `EoAddress` 定长数组 + Config/Reconfig 协议 |
| [0020](0020-seq-version-control.md) | AiChatBus seq 版本控制 | 抢占式请求——新请求到达时旧响应自动丢弃 |
| [0025](0025-frontend-constraints.md) | 前端行为约束汇总 | 单连接单 AppType、username 最长 12 字符、注册登录分离 |

## 七、接口与测试（6 篇）

接入层优化、UT 规范、日志。

| 编号 | 标题 | 核心决策 |
|------|------|---------|
| [0024](0024-head-accesstype-reuse.md) | accessType 复用替代 sourceAddress | 回程路由由 `accessType` + Gateway `adapterTable_` 完成 |
| [0026](0026-task-sync-message.md) | TaskSync 统一同步消息 | 多端 Task 创建/删除事件的统一通知格式 |
| [0027](0027-adapter-adaptive-polling.md) | AccessAdapter 自适应轮询 | idle > 30s 时 poll 超时从 100ms 退避至 1s |
| [0028](0028-eo-ut-design-rules.md) | EO 类 UT 设计规范 | EoTestBase 零 CAF 痕迹；stub 生命周期管理 |
| [0029](0029-non-eo-testable-class.md) | 非 EO 类 UT 路由机制 | 三态（orig/empty/mock）编译期类替换 |
| [0030](0030-syslog.md) | SysLog 编译期过滤日志 | 配置文件 → Python 脚本 → constexpr 头文件；`if constexpr` 消除死代码 |

---

## 模板

```markdown
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
```

| 方案 | 否决原因 |
|------|----------|

## 影响

