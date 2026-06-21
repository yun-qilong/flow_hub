# ADR-0019：Router 路由表实现——定长数组 + Config/Reconfig 协议 + 混合转发策略

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-21 | 韵启龙 |

---

## 背景

ADR-0011 确立了 GTID 替代虚拟 ID 作为路由键，Router 定位为 Business Layer 内部设施。在此基础上需要确定：

1. Router 内部路由表的数据结构
2. 路由表配置与运行时更新的协议
3. 业务消息的转发策略

---

## 决策

### 1. 路由表数据结构：`std::array<fw::EoAddress, 1024>`

GTID 的 `TaskType` 字段为 10 位（`GTID >> 6`），理论最大 1024 个值。直接用定长数组，索引即 `TaskType` 值。

```cpp
static constexpr uint16_t kRouteTableSize = 1024;
std::array<fw::EoAddress, kRouteTableSize> routeTable_{};
```

| 维度 | 说明 |
|------|------|
| 内存 | 1024 × 16 字节 ≈ 16 KB（`EoAddress = caf::actor`） |
| 查表 | `routeTable_[gtid >> 6]` — 单次下标，O(1)，零哈希开销 |
| 哨兵 | 默认构造的 `caf::actor` 为无效句柄，`if (addr)` 即可判空 |
| 稀疏容忍 | 当前 3 个 TaskType，理论最大 192，81% 浪费 ≈ 13 KB — 可接受 |
| 后续迁移 | 接口不变，内部可切换为编译期生成的路由表 |

### 2. 查表函数：`getTargetEoAddress`

```cpp
fw::EoAddress getTargetEoAddress(uint16_t gtid) const {
    return routeTable_[gtid >> 6];
}
```

单一 GTID 到地址的映射，不作遍历。`gtidList` 的遍历逻辑由上层封装。

### 3. 配置协议：`RouterConfigReq` / `RouterReconfigReq`

| 消息 | 用途 | 字段 | 回复 |
|------|------|------|------|
| `RouterConfigReq` | 全量下发（setup 阶段） | `EoAddress[1024] addresses` | `RouterConfigResp{success}` |
| `RouterReconfigReq` | 增量更新（运行时） | `vector<RouteEntry> entries` | `RouterReconfigResp{success}` |

`RouteEntry` 结构：
```
struct RouteEntry
    TaskType taskType
    EoAddress address
```

Router 处理逻辑：

```cpp
// Config：全量覆盖
routeTable_ = req.addresses;
replyToSender(RouterConfigResp{true});

// Reconfig：逐项更新
for (auto &entry : req.entries) {
    routeTable_[static_cast<uint16_t>(entry.taskType)] = entry.address;
}
replyToSender(RouterReconfigResp{true});
```

发送方：setup 阶段由 BusinessMgr（验证期由 main 代）下发全量路由表；运行时热备/负载均衡调整时由 BusinessMgr 发 ReconfigReq。

### 4. 回复机制：`replyToSender`

使用 CAF 的 `make_response_promise().deliver()` 实现，确保与发送方 `requestThen` 的 `.then()` 回调正确匹配：

```cpp
template <typename Msg>
void replyToSender(Msg &&msg) {
    auto rp = this->make_response_promise();
    rp.deliver(caf::make_message(std::forward<Msg>(msg)));
}
```

兼容性：若发送方是 `anonSendTo`（fire-and-forget），`source` 为无效 actor，回复静默丢弃——对验证期 main 发送的场景无副作用。

### 5. 转发策略：`routeAndForward` 混合模式

遍历 `gtidList`，前 n-1 项查表命中用 `sendTo`（拷贝），最后一项命中用 `delegateTo`（零拷贝移动）：

```cpp
template <typename Msg>
void routeAndForward(Msg msg, const char *msgName) {
    const auto &list = msg.head.gtidList;
    size_t n = list.size();
    if (n == 0) { /* drop */ return; }

    // 前 n-1 项：拷贝转发
    for (size_t i = 0; i < n - 1; ++i) {
        auto addr = getTargetEoAddress(list[i]);
        if (addr) { sendTo(addr, Msg{msg}); }
    }

    // 最后一项：零拷贝转发
    auto lastAddr = getTargetEoAddress(list[n - 1]);
    if (lastAddr) { delegateTo(lastAddr, std::move(msg)); }
    else { /* drop */ }
}
```

| 场景 | 行为 |
|------|------|
| 单 GTID 命中 | 直接 `delegateTo`，零拷贝 |
| 多 GTID 命中 | 前 N-1 个 `sendTo`（拷贝），最后 1 个 `delegateTo`（移动） |
| 全未命中 | drop + stderr 日志 |
| 空 gtidList | drop + stderr 日志 |

`delegateTo` 要求消息非 const，因此业务消息 handler 采用值传递（`Msg req`），配合 `onMsg` 的 `std::move` 语义（ADR-0018）。

### 6. .mt 类型别名：统一使用 `EoAddress`

在 `types.mt` 中定义 `define EoAddress = actor`，所有 `.mt` 消息定义使用项目命名 `EoAddress`，彻底屏蔽 CAF 类型名：

```
// types.mt
define GTID = uint16
define EoAddress = actor

// routeEntry.mt
struct RouteEntry
    TaskType taskType
    EoAddress address
```

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| `unordered_map<TaskType, EoAddress>` | 动态分配，热路径哈希开销 |
| `array<optional<EoAddress>, 1024>` | `caf::actor` 自带无效哨兵，无需 `optional` |
| 全 `sendTo` 拷贝转发 | 每次转发都深拷贝 string/vector 字段，浪费 |
| 全 `delegateTo` 移动转发 | 多目标 fan-out 时只能移动一次，后续未定义行为 |
| 两趟扫描找最后命中 | 过度设计；向量最后一个位置天然适合做零拷贝候选 |

---

## 影响

- 新增 `RouterConfigReq`/`RouterConfigResp`/`RouterReconfigReq`/`RouterReconfigResp`/`RouteEntry` 五组消息
- `ModifyReq` 在 Router 侧的职责被 `RouterConfigReq` 替代
- BusinessMgr（验证期由 main）在 setup 阶段下发全量路由表
- `replyToSender` 加入 `EoBase`，为 request-response 模式提供接收端支持
- 所有 `.mt` 文件统一使用 `EoAddress` 替代 `actor` 类型名
