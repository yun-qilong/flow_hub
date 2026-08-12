# 双底座 OSAL 设计：让 flowHub 同时跑在 Linux + CAF 与 Zephyr RTOS 上

| 状态 | 日期 | 作者 |
|------|------|------|
| 预研（设计阶段） | 2026-08-08 | 韵启龙 |

---

## 1. 背景与目标

flowHub 当前运行于 Linux + CAF（C++ Actor Framework）之上，核心哲学：

- **全消息驱动的 Actor 模型**：每个业务逻辑单元（EO）是独立 Actor，无状态，状态全在跟 Task 走的 Context 中
- **无锁**：模块间只走消息，不用互斥锁
- **零堆分配**：Context 全在静态内存池，热路径无 `malloc`
- **可预测性**：cache line 隔离防伪共享，编译期标签 `kMayBlock` 决定调度策略

这套设计哲学并非凭空而来——它继承自诺基亚 5G L2 MAC 上行调度系统的工程实践。诺基亚的代码运行在一个魔改的大型 RTOS 上：线程绑核追求极致可预测性，模块间消息通信、无锁。flowHub 选择 CAF，正是为了最大程度保留这套"无锁消息驱动"的系统设计理解。

### 1.1 为什么要双底座

- **Linux + CAF**（现状）：开发便利、生态完整，适合验证与快速迭代
- **Zephyr RTOS**（目标）：资源受限、确定性调度，面向嵌入式/工控/设备侧部署

目标不是"切换"，而是**双底座**：同一套上层业务代码（EO 体系）在两个平台上都能运行。业务开发者只需面向 `fw/` 层的 EO 接口编程，不感知底层是 CAF 还是 Zephyr。

### 1.2 与诺基亚 RTOS 经验的映射

| 诺基亚魔改 RTOS | flowHub 现状（CAF） | Zephyr 目标 |
|----------------|--------------------|-------------|
| 线程绑核追求可预测性 | CAF 调度池（共享/独立线程由 `kMayBlock` 决定） | 固定优先级线程，可 CPU affinity |
| 模块间消息通信、无锁 | `mail()` / `anon_mail()` | `k_msgq`（ISR-safe） |
| EO 无状态、状态跟 Task 的 Context 走 | `TaskPool` 静态内存池 + GTID 索引 | `TaskPool` 直接复用（已纯静态） |
| 任务静态创建，不动态增减 | `EoEnv::createEo<T>()` | 静态线程 + 静态消息队列 |

这套映射是双底座设计可行性的**第一性依据**：Zephyr 提供的原语（消息队列、静态线程、内存 slab）与诺基亚 RTOS 的核心机制同构，因此 flowHub 的架构哲学可以低成本迁移。

---

## 2. CAF 依赖分析与提取

### 2.1 现状耦合点

对 `src/` 的排查确认，`caf::` 依赖已收敛在 `fw/` 层，业务代码基本无感知（这正是 ADR-0016 EoEnv 包装层的成果）：

| 文件 | 对 CAF 的依赖 | 耦合度 |
|------|--------------|--------|
| `fw/EoTypes.hpp` | `EoAddress = caf::actor`、`MessageHandler`、`EoConfig`、`EoSystemConfig`、`EoDuration` | 高（类型别名直接映射） |
| `fw/EoEnv.hpp` | `caf::actor_system`、`spawn`、`anon_mail`、`await_all_actors_done`、全局元对象初始化 | 高（持有系统对象） |
| `fw/EoBase.hpp` | 继承 `caf::event_based_actor`、`caf::message_handler`、`mail().send/delegate/delay/request`、`make_response_promise` | 最高（actor 基类） |
| `fw/ScopedEo.hpp` | `caf::scoped_actor`、`receive_for` 轮询 | 高 |
| `fw/EoTestBase.hpp` | `caf::scoped_actor`、`anon_send_exit`、`caf::after` | 中（仅测试） |
| `fw/MessageRoundTrip.hpp` | `caf::binary_serializer/deserializer` | 中（仅测试） |
| `fw/StaticVectorCaf.hpp` | CAF inspect 特化 | 低（序列化支持） |
| `userAccess/AccessAdapterBase.hpp`、`CliAdapter.hpp` | 持有 `caf::actor_system&` | 低（仅传引用） |
| `main.cpp` | `env.system()` 传给 CliAdapter | 低 |

### 2.2 提炼出的能力域

| 能力域 | 必要性 | 对应 CAF 机制 |
|--------|--------|--------------|
| Actor 句柄与地址 | 必须 | `caf::actor` / `actor_cast` |
| 消息传递（tell/delegate/anon/request-reply/delay） | 必须 | `mail()` / `anon_mail()` / `response_promise` |
| 任务模型（spawn/awaitAllDone/stopAll） | 必须 | `actor_system::spawn` / `await_all_actors_done` |
| 时间管理（Duration/now/sleep/delay） | 必须 | `caf::timespan` / `steady_clock` |
| 内存管理（消息分配/释放） | 必须 | CAF 内存分配（关键路径可换静态池） |
| 序列化（round-trip） | 可选（仅测试） | `binary_serializer` / inspect |

---

## 3. OSAL 设计

### 3.1 设计原则

1. **编译期多态，零运行时开销**：不引入虚函数。通过后端选择宏（`FLOWHUB_OSAL_BACKEND`）+ 类型别名/模板策略在编译期绑定实现
2. **保持 EO 接口不变**：业务代码仍写 `EoBase<Derived>` + `onMsg<X>()`，不感知后端
3. **ISR-safe 能力显式标注**：接口文档中标注每个调用是否可用于中断上下文
4. **最小侵入**：先冻结接口，再逐层替换

### 3.2 接口域定义（函数签名级）

```
// ---- 1. Actor 句柄与地址 ----
class ActorAddr;                    // 不透明，可拷贝、可比较、可哈希
ActorAddr senderAddr();             // 消息处理期间有效，返回当前消息发送者
ActorAddr myAddr();                 // 当前 EO 自身地址

// ---- 2. 消息传递 ----
void sendTo(ActorAddr dest, Msg&& msg);          // 非阻塞，fire-and-forget
void delegateTo(ActorAddr dest, Msg&& msg);      // 转发，保留原 sender（透传）
void anonSendTo(ActorAddr dest, Msg&& msg);      // 匿名发送（无 sender）
RequestId requestThen(ActorAddr dest, Msg&& msg, Duration timeout,
                      OnValue&&, OnError&&);     // 异步请求-响应，带超时
void replyToSender(Msg&& msg);                   // 回复当前请求的发送方
void delaySendTo(ActorAddr dest, Duration d, Msg&& msg);  // 延迟发送，非阻塞

// ---- 3. 任务模型 ----
class System {
  template<typename T, typename... Args>
  ActorAddr spawn(Args&&...);      // 由 T::kMayBlock 决定调度策略
  void awaitAllDone();
  void stopAll();
};

// ---- 4. 时间管理 ----
class Duration;                     // 单调时钟，微秒精度
Duration now();
void sleep(Duration);               // 仅阻塞型 EO 可用

// ---- 5. 内存管理 ----
MsgBuffer allocMsg(size_t size);    // 零堆分配，静态 slab，ISR-safe
void freeMsg(MsgBuffer);            // 归还 slab

// ---- 6. 序列化（仅测试） ----
bool serialize(const Msg&, BinaryBuffer&);
bool deserialize(Msg&, const BinaryBuffer&);
```

### 3.3 CAF 后端（最小改动封装）

CAF 后端即现有 `fw/` 层的薄封装——把 `caf::actor_system`、`caf::event_based_actor` 等通过 OSAL 类型别名和包装类收口，业务代码零改动。此端为**主端**，OSAL 接口冻结后先适配它，确保现有系统行为不变（作为回归基线）。

### 3.4 Zephyr 后端设计

#### EO → 线程映射

初期采用**每 EO 一个线程 + 一个消息队列**的简单模型（与 CAF 的独立 actor 语义一致，最易保证确定性）：

```cpp
// 每个 EO 的静态控制块
struct EoCtlBlock {
  k_msgq queue;        // 容量 8~16（按内存调整）
  k_thread thread;     // 静态定义
  k_thread_stack_t stack[EO_STACK_SIZE];
  osal::ActorAddr addr;
};

void eoThread(void* arg) {
  auto* ctl = static_cast<EoCtlBlock*>(arg);
  while (true) {
    MsgBuffer msg = allocMsg();                    // 从 slab 取
    k_msgq_get(&ctl->queue, &msg, K_FOREVER);      // 阻塞等待
    invoke_behavior(ctl, &msg);                    // 调用 EO 的 onMsg 分发
    freeMsg(msg);
  }
}
```

#### kMayBlock 语义保持

- `kMayBlock == true` 的 EO → 独立线程，行为内可阻塞（`k_sleep`、`k_msgq_get` with timeout）
- `kMayBlock == false` 的 EO → 独立线程，但行为必须快速返回（与 CAF 共享池的约束一致）

初期不做共享工作线程池（那会引入调度抖动，与"可预测性"目标冲突）；EO 数量在 Zephyr 场景下有限（每任务类型一个），静态分配可接受。

#### 消息路由

- `sendTo(dest, msg)` → 从全局消息 slab 分配消息块 → 填 sender/receiver 字段 → `k_msgq_put(&dest->queue, &msg_ptr, K_NO_WAIT)`
- 队列满：丢弃并记录错误（或返回失败），由上层背压策略处理——**这是有意的设计选择**，与 CAF 无界邮箱不同，体现对资源受限平台的诚实
- **中断上下文**：`k_msgq_put` 与 `k_mem_slab_alloc` 均 ISR-safe（K_NO_WAIT 无阻塞），可在中断中投递消息

#### 内存管理

- 消息 slab：`k_mem_slab_init` 预分配若干（如 64）个"最大消息大小"块
- `allocMsg()` 无等待，返回 `nullptr` 表示过载——调用方必须有背压/丢弃策略
- `TaskPool`（GTID Context 存储）**直接复用**：已确认纯静态、无 CAF 依赖

#### 时间与延迟发送

- `Duration` 用 64 位微秒，基于 `k_cyc_to_us_floor64`（注意 32 位回绕处理）
- `delaySendTo`：一次性 `k_timer`，超时回调中投递；或 `k_work_delayable` 提交到系统工作队列（工作队列有专用线程，可安全分配 slab）

---

## 4. 分层集成与业务代码影响

### 4.1 分层图

```
┌──────────────────────────────────────────────────────┐
│  业务代码 (userAccess / DPlane / CPlane)             │  ← 只依赖 Eo 接口
├──────────────────────────────────────────────────────┤
│  fw/ 层 (EoBase / EoEnv / EoTypes / ScopedEo)        │  ← 只使用 OSAL 接口
├──────────────────────────────────────────────────────┤
│  OSAL 抽象层 (osal/) — 纯头文件                       │
│    · osal/Actor.hpp · osal/System.hpp · osal/Timer.hpp
│    · osal/MsgMem.hpp · osal/Serialization.hpp · osal/OsalConfig.hpp
├──────────────────────────────────────────────────────┤
│  CAF 后端 (osal/caf/)          Zephyr 后端 (osal/zephyr/)
└──────────────────────────────────────────────────────┘
```

### 4.2 现有文件改造清单

| 文件 | 改造 |
|------|------|
| `fw/EoTypes.hpp` | `EoAddress = osal::ActorAddr` 等，不再引用 CAF |
| `fw/EoEnv.hpp` | 内部持 `osal::System`；`system()` 不再暴露原生引用（或改为返回 `osal::System&`） |
| `fw/EoBase.hpp` | 继承 `osal::ActorBase`（后端提供）；`onMsg/sendTo` 等改用 OSAL 接口 |
| `fw/ScopedEo.hpp` | 提供 `osal::ScopedActor`（Zephyr 端初期可不实现） |
| `fw/EoTestBase.hpp` | 测试工具分离：CAF 专用保留，Zephyr 用简化版 |
| `fw/StaticVectorCaf.hpp` | 序列化支持改走 OSAL |
| `AccessAdapterBase.hpp` / `CliAdapter.hpp` | `caf::actor_system&` → `EoEnv&`（改动 ≤5 行/文件） |
| `main.cpp` | 不把 `env.system()` 传出；CliAdapter 改收 `EoEnv&` |
| 生成代码 `Messages.hpp` | `caf::inspect` → `OSAL_INSPECT`（生成脚本改动可控，约 50 行） |

### 4.3 业务代码最小改动集

- 所有 EO 仍继承 `EoBase<Derived>`，**不需要感知后端**
- 直接持有 `caf::actor_system&` 的文件仅 `AccessAdapterBase` / `CliAdapter`，改为 `EoEnv&` 即可
- 构建系统：CMake 选项 `FLOWHUB_OSAL_BACKEND={caf|zephyr}` 切换

---

## 5. 难点与对策

### 5.1 中断上下文消息

- **对策**：`k_msgq_put` + `k_mem_slab_alloc` 均 ISR-safe（K_NO_WAIT）；中断处理必须快速、无阻塞
- **设计约束**：中断中不执行业务行为，只投递消息，由线程上下文处理——保证确定性

### 5.2 内存确定性

- **对策**：全静态分配（TaskPool + 消息 slab），编译期验证最大占用；`allocMsg` 失败即过载信号
- **对比**：CAF 邮箱无界（可动态增长），Zephyr 必须显式有界——这是**特性差异**，不是缺陷，需在文档中诚实声明

### 5.3 调度抖动

- **对策**：每 EO 独立线程 + 固定优先级；不做共享池；可配置 CPU affinity（对应诺基亚绑核经验）
- **测量**：阶段 2 最小验证中量化（目标 99% 抖动 < 10µs）

### 5.4 kMayBlock 语义

- **对策**：独立线程承载阻塞 EO；非阻塞 EO 行为快速返回。语义与 CAF 端完全一致（ADR-0017 的编译期标签沿用）

### 5.5 与诺基亚 RTOS 经验的映射（加分点）

诺基亚魔改 RTOS 的核心实践——**线程绑核、模块间无锁消息、任务静态创建、状态与执行体分离**——在 Zephyr 上均有同构对应（CPU affinity、k_msgq、静态线程、TaskPool）。这意味着双底座不是"重新设计一套架构"，而是**把已经在大规模商用系统验证过的确定性设计范式，平移到资源受限平台**。这是本项目区别于"从零学 RTOS"候选人的根本差异。

---

## 6. 验证计划

| 阶段 | 内容 | 验收标准 |
|------|------|---------|
| 阶段 1（设计冻结） | OSAL 接口冻结 + CAF 后端适配 | 现有 Linux 系统行为不变（回归通过） |
| 阶段 2（最小验证） | Zephyr（QEMU）上 Ping-Pong：两个 EO 单向消息往返 | 往返延迟 < 50µs；99% 抖动 < 10µs；消息丢失 0 |
| 阶段 3（能力补齐） | 逐步加入 delaySendTo、request-response、序列化 | 各项能力在 Zephyr 上可用，性能达标 |

阶段 2 最小集：OSAL 纯头 + Zephyr 后端（System/Address/sendTo/消息 slab）+ `PingEo`/`PongEo`。暂不做：requestThen、序列化、EoTestBase/ScopedEo、delaySendTo、or_else 链（用 switch 按消息 ID 分发）。

---

## 7. 风险与可行性边界

### 7.1 不迁移的 CAF 特性（诚实声明）

| CAF 特性 | 决策 | 理由 | 现有依赖 |
|----------|------|------|---------|
| 动态 actor 创建/销毁 | 限制为静态池 + 编译期上限 | Zephyr 线程需预定义栈/控制块，动态创建破坏确定性 | `createEo<T>` 中 T 种类编译期已知，可接受 |
| actor 监控/崩溃重启 | 降级 | Zephyr 无内建 link/monitor；用 `awaitAllDone` + Watchdog 替代 | 未依赖复杂监控 |
| 分布式/远程 actor | 不迁移 | 需要网络栈与序列化协议，资源不允许 | 未使用 |
| typed actor / 流式消息 | 不迁移 | 无实际需求 | 未使用 |
| or_else 消息链合并 | 简化为函数表/switch | CAF 机制复杂，Zephyr 用简单分发 | `EoBase::on<>` 内部使用，可重实现 |
| 跨进程序列化 | 替换为轻量方案 | 仅测试用 | 测试代码 |

### 7.2 可行边界结论

**核心实时消息调度引擎（Actor 地址、tell、delegate、request-response、延迟发送）可 1:1 迁移**。EO 实例数量编译期静态配置——这与诺基亚基站 RTOS 的做法一致（任务全部静态创建），业务上完全可接受。**双底座在"核心实时消息调度"范围内完全可行**，维持无锁、可预测、静态内存的核心哲学。

---

## 8. 结论

双底座 OSAL 设计成立的关键论据有三：

1. **同构性**：flowHub 的架构哲学（无锁消息、静态内存、状态外置）与 Zephyr 原语天然同构，且这套哲学已在诺基亚大规模商用 RTOS 上验证过
2. **收敛性**：CAF 依赖已收敛在 `fw/` 层（ADR-0016 的成果），业务代码零感知，改造面可控
3. **边界清晰**：通过限制动态创建等高级特性，在核心消息调度范围内实现 1:1 迁移；对不迁移的特性有诚实声明与替代方案

当前状态：**设计阶段**。下一步为阶段 1（OSAL 接口冻结 + CAF 后端回归），随后可进入阶段 2 最小验证。
