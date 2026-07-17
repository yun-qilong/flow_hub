# ADR-0024：head.accessType 复用替代 sourceAddress 字段

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-01 | 韵启龙 |

---

## 背景

旧设计中，消息头携带 `sourceAddress` 字段（`EoAddress` 类型），用于接收方回复消息时知道往哪回。

**sourceAddress 最初为何存在**：架构设计初期遵循一条原则——**上层持有下层地址，下层不持有上层地址**。原因很自然：上层知道"把消息发给谁"，但下层是通用服务，不应知道"谁在调用我"。由此产生一个问题——当下层需要回复时（如 Business EO 回复 SessionData、Service Adapter 回复 Business EO），它不知道往哪回。`sourceAddress` 就是为解决这个问题而设：发送方在消息中附上自己的地址，接收方直接 `sendTo(sourceAddress, reply)` 即可。

**为何后来可以移除**：随着架构细化，每一层 D 面的入向/出向都收敛到**唯一入口 EO**：

| 层 | 入向入口 | 出向入口 | 地址由谁持有 |
|----|---------|---------|------------|
| Session | SessionData | — | AccessGateway 持有 SessionData 地址 |
| Business | Router | — | SessionData 持有 Router 地址 |
| Business→Service | — | ServiceGateway | 各 Business EO 持有 ServiceGateway 地址 |
| Service | — | ServiceGateway | Service Adapter 注册时获取 ServiceGateway 地址 |

这些入口 EO 的地址在 setup/注册阶段就已互相知晓，不存在"不知道往哪回"的问题。因此 `sourceAddress` 在所有层都变得冗余——不仅仅是 Session↔Access 层。

具体而言：

1. **架构拓扑固定**：各层之间的通信路径是确定的——下行 `Adapter→Gateway→SessionData→Router→BusinessEO`，上行 `BusinessEO→SessionData→Gateway→Adapter`。不存在需要动态记录"从哪来"的场景。

2. **Access Adapter 回程由 Gateway 的 `adapterTable_` 决定**：Gateway 维护 `adapterTable_[MAX_ACCESS_TYPES]` 映射（AccessType → EoAddress）。发往 Access Adapter 的消息只需知道目标 AccessType，Gateway 负责查表路由。不需要 `sourceAddress`。

3. **Business 层回复直连已知地址**：Business EO 回复 SessionData（上行 ACK/回复）时，SessionData 地址在 setup 阶段已知；Business EO 发往 Service 层时，ServiceGateway 地址已知。Service Adapter 入向回复直达 Router——Router 地址在 Service Adapter 初始化时已知。

4. **Fan-out 源标识由 `accessType` 天然承担**：在 BatchFanOut/FanOutMsg 机制（ADR-0022）中，`head.accessType` 是源 Adapter 填入的自身 AccessType。Gateway 不重写 `head.accessType`，因此它天然标识了消息的源头。Adapter 收到 FanOutMsg 后比较 `head.accessType == myAccessType` 即可判断自己是源还是其他端——不需要独立的 `sourceAddress` 字段。

5. **`accessType` 已全程携带**：消息头中 `accessType` 字段由 Adapter 编译期填入，随消息全链路流转。它已经提供了足够的源头标识信息。

## 决策

### 从消息头中移除 `sourceAddress`

消息头字段定稿为：

| 字段 | 类型 | 填入者 | 说明 |
|------|------|--------|------|
| `uid` | uint16_t | Adapter | 用户标识，自携带 AppType |
| `gtidList` | GTID 列表 | 前端（或 SessionMgr 分配后替换） | 序列号全 1 = 新 Task 哨兵 |
| `accessType` | AccessType | Adapter（编译期常量） | 源 Adapter 标识；Gateway 查表回程路由；fan-out 源排除 |
| `appType` | AppType | Adapter（编译期常量，AccessType 隐含） | 控制面请求携带 |
| `sessionFlags` | SessionFlags | Adapter（`make<AppType>()` 编译期构造） | 行为标志 |

`accessType` 承担三重职责：
1. **回程路由**：Gateway 据 `accessType` 查 `adapterTable_` 将响应发回正确的 Adapter
2. **Fan-out 索引**：SessionData 据 `accessType` 在 `userAccessBitset[uid]` 中置位/清位
3. **源 Adapter 排除**：Adapter 收到 FanOutMsg 后比较 `head.accessType == myAccessType` 判断自己是源还是其他端

### 不新增 `sourceAccessType` 独立字段

`head.accessType` 已经是源标识——它由源 Adapter 填入，且被决策为"Gateway 不重写"。在 FanOutMsg 中，`head.accessType` 保持原始值不变，因此天然就是源标识。

若新增 `sourceAccessType` 独立字段，则它与 `accessType` 在 FanOutMsg 场景中完全等值——冗余。

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 保留 `sourceAddress` | 架构拓扑固定，回程路由由 `adapterTable_` 查表完成；`accessType` 已提供足够标识信息 |
| 新增独立 `sourceAccessType` 字段 | 与 `accessType` 在 FanOutMsg 中等值——冗余字段 |
| Gateway 重写 FanOutMsg 的 `accessType` 为自身 | 丢失源标识信息，Adapter 无法区分送达/同步 |
| 移除 `accessType`，仅用 `sourceAddress` | `accessType` 还承担 fan-out 位图索引职责，`sourceAddress`（EoAddress）无法替代位图操作 |

## 影响

- **消息头**：移除 `sourceAddress` 字段，字段数从 6 减为 5。
- **Session 层 → Access 层回程**：由 Gateway 查 `adapterTable_[accessType]` 完成，替代原来的 `sendTo(sourceAddress)`。
- **Business EO → SessionData（上行 ACK/回复）**：SessionData 地址在 setup 阶段已知，Business EO 直发，无需 `sourceAddress`。
- **Business EO → ServiceGateway（出向请求）**：ServiceGateway 地址已知，直发即可。
- **Service Adapter → Router（入向应答）**：Router 地址在 Service Adapter 初始化时已知，不再从 `sourceAddress` 提取。
- **Router**：`sourceAddress` 字段消失后，Router 不再需要关心回复地址——减轻 Router 职责。
- **Access Adapter**：源判断逻辑统一为 `head.accessType == myAccessType`。
- **与旧代码的兼容**：Session、Business、Service 层现有代码中依赖 `sourceAddress` 做回程路由的逻辑需同步修改。
