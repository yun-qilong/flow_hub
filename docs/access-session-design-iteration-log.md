# 接入层与会话层 — 迭代记录

> 逐轮确认过程中的所有决策、变更、否决方案及对后续轮次的影响追踪。
> 详细 checklist 见 `docs/access-session-design-checklist.md`。

---

### G. Round 1 迭代记录

> 记录 Round 1 确认过程中的关键变更及对后续轮次的影响。

#### G-1. Round 1 变更清单

| # | 变更项 | 旧 | 新 | 影响轮次 |
|---|--------|----|----|----------|
| 1 | 类型命名 | SessionType | AppType | 全文 |
| 2 | AppType 可见范围 | 止于 SessionMgr | 止于 SessionData | R3, R8 |
| 3 | AppType 携带方式 | SessionData 本地表 | UserHead 字段（adapter 编译期填入） | R2, R4 |
| 4 | SessionMgr 职责 | 三件事（GTID过滤+校验+上下文同步决策） | 一件事（NewSessionReq 闸门校验） | R7 |
| 5 | userId 编码 | uint8_t，字符串拼接 username+"_"+appType | uint16_t [userId:8][AppType:8] | R2, R9 |
| 6 | 命名体系 | userIndex（uint8_t）→ userId（uint16_t composite） | userId（uint8_t）+ uid（uint16_t composite） | 全文 |
| 7 | userAccessBitset | MAX_USERS=64 的 array | MAX_UID=65536 的 flat array（512KB） | R2, R6 |
| 8 | sessionFlags 填入者 | 未明确 | Adapter 编译期填入（开发者不遗漏） | R4, R5 |
| 9 | 业务消息路径 | 未明确中间层行为 | 全链路透传，BusinessEO 收 Adapter 原始消息 | R4, R5 |
| 10 | AppType 与 TaskType 关系 | "映射表" | "集合包含"（AppType 包含一组 TaskType） | R2 |

#### G-2. 后续轮次快照对比

**Round 2（数据结构）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| userId 类型 | uint8_t | uint16_t（uid），uint8_t（userId） |
| usernameToId_ | key = username+"_"+appType | username → userId |
| UserRecord | array[64]，每项 vector<GTID,256> | array[64][64]，每项 static_vector<GTID,128> |
| userAccessBitset | array<uint64_t, 64> | array<uint64_t, 65536>（512KB flat） |
| 新增 | — | MAX_UID, MAX_APP_TYPES 常量 |

**Round 3（SessionData）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 决策模式 | "双模式（透传+编排）" | 不变，但透传为 GTID 粒度，编排仅限 UserLogin 触发 |
| AppType 感知 | "不感知 AppType" | "据 AppType 决定初始动作，后续透传" |

**Round 4（消息下行）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| sessionFlags 填入 | "在哪个节点填入？"（待定） | Adapter 编译期填入 |
| 中间层行为 | 未明确 | Gateway/SessionData/Router 仅转发 |

**Round 7（控制面生命周期）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| D2 GTID 列表 | "GTID 过滤" | "GTID 列表天然合法"（闸门校验已保证） |
| 上下文同步决策 | SessionMgr 决定 | SessionData 自行据 GTID 列表判断 |

**Round 8（数据面生命周期）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 消息同步归属 | "SessionData 据 AppType 执行" | "Adapter 编译期 sessionFlags 控制，BusinessEO 读标志执行" |
| ACK 决策者 | 未明确 | Adapter（编译期）→ BusinessEO（读标志） |

**Round 9（跨切面）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 常量 | MAX_USERS=64 等（散落各处） | 7 个常量集中汇总在 R2 |
| sessionFlags 位定义 | 未定 | 待 R2 确认后续轮次展开时确定位布局 |

#### G-3. 关键决策时间线

1. **SessionType → AppType** — "Session 这个词跟架构层名冲突"
2. **AppType 可见范围回退** — "SessionData 收到了 UserLogin 里的 appType，说它不感知是反自然的"
3. **闸门模型确立** — "建 task 时校验，登录 GTID 列表天然干净"
4. **uid 编码方案** — "userId 自携带 AppType，无需字符串拼接，无需本地表"
5. **userAccessBitset 选 flat** — "512KB 换零翻译，D 面操作频繁，值"
6. **sessionFlags 归 Adapter** — "不希望 Business 层判断 AppType，adapter 编译期填入，开发者不遗漏"

---

### H. Round 2 迭代记录

> 记录 Round 2 确认过程中的关键变更及对后续轮次的影响。

#### H-1. Round 2 变更清单

| # | 变更项 | 旧 | 新 | 影响轮次 |
|---|--------|----|----|----------|
| 1 | 常量组织 | 散落在各节末尾 | 收拢到 2.1 先定常量，后续各节引用 | — |
| 2 | MAX_ACCESS_TYPES 与 MAX_APP_TYPES 关系 | 未说明为何同为 64 | 池化概念：最多 64 个 Adapter，AppType 数不可能超过 Adapter 数 | — |
| 3 | BatchCounter 设计 | 仅结构 `{total, received}` | 封装为类，allocate() 返回 token，凭 token 定位 counter | R7, R8 |
| 4 | uidBitset 描述 | "仅 userIndex 维度" | "仅 userId 维度"，去掉 userIndex 旧术语 | — |

#### H-2. 后续轮次快照对比

**Round 3（SessionData）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 展开程度 | 大纲三行（透传/执行/不做） | 展开为 3.1~3.3，含三种触发源、执行逻辑 |
| UserLogin 触发 | 未明确 | 据 AppType 决定上下文同步，同时更新 bitset |
| AiChatBus 上行 | "收到 ACK/回复 → fan-out" | 统一处理，不区分 ACK 和回复 |
| UserModify 触发 | 未在 R3 中列出 | 新增为第三触发源 |
| 不做清单 | 一行 | 六条硬边界（不分配GTID/不维护映射/不直连Adapter等） |

**Round 4（消息下行）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| sessionFlags 填入者 | Adapter（R1 已定） | 不变 |
| 节点行为 | 未展开 | 未展开（待 R3 确认后） |

**Round 5（消息上行）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| ACK 发送条件 | sessionFlags 控制（R1 已定） | 不变 |
| ACK 与回复路径 | 待定是否共用 | 待展开（R3 已明确统一 fan-out，R5 细化） |

**Round 6（Fan-out）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 过滤逻辑 | 待定 | 待展开，但 R2 确认的 512KB flat bitset 为 O(1) 索引奠定基础 |

**Round 7（控制面生命周期）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| D2 GTID 列表 | "天然合法"（R1 已定） | 不变 |
| 上下文同步 | SessionData 自行判断（R1 已定） | 不变 |

**Round 8（数据面生命周期）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 消息同步 | Adapter sessionFlags + BusinessEO 读标志（R1 已定） | 不变 |
| BatchCounter | 仅结构定义 | 封装为类，token 机制，影响 D2 上下文同步流程 |

**Round 9（跨切面）**

| 项 | 确认前 | 确认后 |
|----|--------|--------|
| 常量 | 7 个常量汇总在 R2（R1 已定） | 位置不变，新增池化关系说明 |

#### H-3. 常量总结

| 常量 | 值 | 池化关系 |
|------|----|----------|
| MAX_USERS | 64 | userId 维度 |
| MAX_APP_TYPES | 64 | ≤ MAX_ACCESS_TYPES（池化约束） |
| MAX_ACCESS_TYPES | 64 | Adapter 总数上限（池） |
| MAX_UID | 65536 | = MAX_USERS × MAX_APP_TYPES |
| MAX_CLIENTS_PER_ACCESS | 64 | 每 Adapter 连接数 |
| MAX_GTIDS_PER_USER | 128 | 每 (userId, appType) 的 GTID 数 |
| MAX_BATCH_COUNTER_NUM | 16 | 并发上下文同步数 |

---

### I. Round 3 迭代记录

> 日期：2026-06-29

#### I-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 3.1 透传模式 | 采纳 | GTID 已决定路由，不需 AppType |
| 2 | 3.2 UserLogin | 采纳 | 置位 + 据 AppType 决定后续；上下文同步非通用，由 AppType 限定 |
| 3 | 3.2 UserLogout | 采纳 | 清位 + D→C response 含活跃 adapter 数 |
| 4 | 3.2 UserReset | 采纳（原名UserCreate） | 分配新 userId 时清空历史 bitset，无需回复 |
| 5 | 3.3 AiChatBus 上行 fan-out | 采纳 | ACK/回复统一路径，AppType 特定行为 |
| 6 | C/D 行为分类 | 采纳 | 3.2=控制面交互（通用），3.3=数据面业务（AppType特定） |
| 7 | 3.4 不做清单 | 采纳 | 6条硬边界 + 1条备忘 |

#### I-2. 否决/调整的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| UserModify 作为独立触发源 | 与 UserLogin/UserLogout 重叠 | 砍掉，login/logout 已覆盖 bitset 更新 |
| UserClear（注销时清空 D 面） | 删除后 uid 无消息，复用前 UserReset 清零即可 | 改为 UserReset，在分配时清，非删除时清 |
| 上下文同步作为通用行为 | 与 AppType 绑定，不应无差别限定 | 改为"据 AppType 决定，例如 AiChat..." |
| 逐 GTID 发 FetchContext | Router 负责解包分发 | 改为批量发往 Router |
| "Mgr 检查活跃 adapter 是否归零" | Mgr 没有 userAccessBitset 数据 | Data 在 response 中告知活跃 adapter 数 |

#### I-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 4 | 无影响，按原大纲展开 |
| Round 7 | UserLogout 需 D→C response 流程；UserDelete 纯 C 面 |
| Round 8 | 消息同步标记为 AppType 特定行为 |
| Round 5~9 | 全部标注行为分类标签 `[纯C面]/[C面交互]/[D面通用]/[AppType特定]` |

---

### J. Round 4 迭代记录

> 日期：2026-06-29

#### J-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 4.1 全链路路径 | 采纳 | Adapter→Gateway→SessionData→Router→BusinessEO→ServiceEO |
| 2 | 4.2 Adapter 行为 | 采纳 | 编译期填入 uid/accessType/appType/sessionFlags |
| 3 | 4.2 Gateway 行为 | 采纳 | 按消息类型 dispatch，原样转发 |
| 4 | 4.2 SessionData 行为 | 采纳 | delegate(Router)，零拷贝，不 fan-out |
| 5 | 4.2 Router 行为 | 采纳 | 按 TaskType 查表 delegate |
| 6 | 4.2 Business EO 行为 | 采纳 | 只读 sessionFlags，不感知 AppType |
| 7 | 4.3 新旧对比 | 采纳 | 旧方案废弃：消息未落地即同步有窗口；新方案 ACK 源头是 Business EO |

#### J-2. 调整项

| 项 | 调整 | 原因 |
|----|------|------|
| Business EO 标签 | `[AppType特定]` → `[D面通用]` | AppType 区分在 Adapter 编译期完成，Business EO 只读标志 |
| 新方案表述 | "fan-out 统一在上行路径" → "ACK 替代消息同步，源头是 Business EO" | Adapter 和 SessionData 在下行路径对同步零动作 |

#### J-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 5 | 展开上行路径：ACK 由 Business EO 发出，SessionData fan-out |
| Round 9 | 新增"消息头字段定稿"项，汇总 uid/gtidList/accessType/appType/sessionFlags |

---

### K. Round 5 迭代记录

> 日期：2026-06-29

#### K-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 5.1 上行路径 | 采纳 | BusinessEO→SessionData→Gateway→Adapter→前端，与下行对称 |
| 2 | 5.2 ACK 触发条件 | 采纳 | `sessionFlags.needsAck` 为 true 时触发，由 Adapter 编译期控制，AiChatBus 读标志执行 |
| 3 | 5.2 ACK 内容 | 采纳 | `seq`（AiChatBus 分配）+ `content`（原消息内容）+ `sourceAccessType`（源 adapter 据此识别自身），对齐 ADR-0020 |
| 4 | 5.2 SessionData 收到 ACK | 采纳 | 读 `uid` → 查 `userAccessBitset[uid]` → 组装 `BatchFanOut` → 发往 Gateway；纯 fan-out，不区分消息类型 |
| 5 | 5.3 回复广播 | 采纳 | AI 回复与 ACK 走完全相同的路径，共用 `BatchFanOut/FanOutMsg` 机制 |
| 6 | 5.4 Gateway→Adapter | 采纳 | Gateway 拆解 `BatchFanOut` 为逐 adapter 的 `FanOutMsg{head, payload}` |
| 7 | 5.4 Adapter 出向 | 采纳 | `userToConn_[userId]` O(1) 查连接；无连接则静默丢弃 |

#### K-2. 否决/调整的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| ACK 由 SessionData 生成 | SessionData 无业务上下文（不知道 seq、content），生成 ACK 需要跨层查询 | ACK 由 Business EO（AiChatBus）生成，SessionData 仅做 fan-out 转发 |

#### K-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 6 | 展开 `BatchFanOut/FanOutMsg` 两级格式细节、各层职责、源 adapter 排除策略 |
| Round 7~9 | 无直接影响 |

---

### L. Round 6 迭代记录

> 日期：2026-06-30

#### L-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 6.1.1 BatchFanOut 字段 | 采纳 | head(uid/gtidList/sourceAccessType/sessionFlags) + payload + targets |
| 2 | 6.1.2 targets 类型 | 采纳 uint64_t | MAX_ACCESS_TYPES=64 填满 64 位；userAccessBitset 同类型直接拷贝 |
| 3 | 6.1.3 SessionData 不修改 targets | 采纳 | 源排除由 Adapter 自判，SessionData 零决策 |
| 4 | 6.1.4 定义位置 | 采纳 common/message/ | SessionData→Gateway 层间消息 |
| 5 | 6.2.3 FanOutMsg 携带 sourceAccessType | 采纳方案 A | Adapter 据此判断 ACK / 消息同步 |
| 6 | 6.2.5 FanOutMsg 独立类型 | 采纳方案 A | 结构体可复用，仅需新类型标识 |
| 7 | 6.3.4 Gateway 不做源排除 | 采纳方案 B | 排除是 Adapter 业务逻辑，Gateway 只做分发 |
| 8 | 6.4.1 排除粒度 | 采纳方案 C — Adapter 自判 | SessionData/Gateway 零决策；源 adapter 需要 ACK 确认 |
| 9 | 6.4.2 源 Adapter 行为 | 采纳 | Adapter 决定发"消息送达"还是"消息同步"，不透传 |
| 10 | 6.5 边界情况 | 采纳 | 三条均静默丢弃 + 至少打 log |

#### L-2. 否决的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| targets 用 StaticBitMap | uint64_t 更简，当前 64 上限够用，扩上限时随常量改 | uint64_t，直接 memcpy userAccessBitset |
| SessionData 排除源 adapter | SessionData 应保持零决策 | Adapter 自判 |
| Gateway 排除源 adapter | Gateway 不应读消息内容做业务判断 | Adapter 自判 |
| Adapter 透传 ACK 给前端 | Adapter 应负责区分送达/同步，不是无脑透传 | Adapter 据 sourceAccessType 发不同消息给前端 |
| 6.4.2 描述"清除超时/重发状态" | 过度设计，当前无此功能 | 简化为 Adapter 发送达/同步通知 |

#### L-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 7 | 无直接影响，C 面生命周期不涉及 fan-out |
| Round 8 | 消息同步（ACK fan-out + 回复广播）已获得完整的 BatchFanOut/FanOutMsg 机制可引用 |
| Round 9 | 消息头字段更清晰：uid/gtidList/accessType/sessionFlags/sourceAccessType 各自归属明确 |

---

### M. Round 7 迭代记录

> 日期：2026-06-30

#### M-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 7.1 D0 注册 | 采纳 | 纯 C 面：分配 userId，初始化 UserRecord，注册与登录分离 |
| 2 | 7.2 D1 首次登录 | 采纳 | C→D 通知 + D→C 回复活跃数；首次无 GTID，不触发上下文同步 |
| 3 | 7.3 D2 后续登录 | 采纳 | 有历史 GTID → SessionData 触发上下文同步 |
| 4 | 7.4 D6 登出 | 采纳 | C→D→C，SessionData 回复活跃 adapter 数 |
| 5 | 7.5 D7 注销 | 采纳 | 纯 C 面：回收 GTID、释放 userId，不通知 D 面 |
| 6 | 前端不发送 appType | 采纳 | Adapter 编译期填入，AccessType 隐含 AppType |
| 7 | C/D 边界原则 | 采纳 | SessionMgr 通知全部已知信息，SessionData 自行决策 |
| 8 | 登录回复保留 | 采纳 | 对称设计 + 冷启动检测（0→1）+ 同步点 |
| 9 | 上下文同步不回 fan-out | 调整 | 数据量大且仅一个前端需要，直接回源 adapter |

#### M-2. 否决/调整的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| 前端发送 appType | Adapter 编译期已知，前端不应关心 | Adapter 填入 |
| 登录不需要 D→C 回复 | 破坏对称性，丢失冷启动检测能力 | 保留回复，含活跃 adapter 数 |
| 上下文同步回复走 fan-out | 历史数据量大，fan-out 到所有 adapter 浪费带宽和算力 | 直接回源 adapter |

#### M-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 8 | D5 断连可直接引用 D6 登出流程；新建/删除会话的 fan-out 通知需引用 SessionData 广播机制 |
| Round 9 | 上下文同步的 BatchCounter 超时策略待定；消息头字段中 appType 由 Adapter 填入已确认 |

---

### N. Round 8 迭代记录

> 日期：2026-06-30

#### N-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 8.1 D3 新建会话 | 调整 | 无独立 NewSessionReq，捆绑请求（控制+数据一起）；ACK fan-out 即通知 |
| 2 | 8.1.2 无 GTID = 新 Task | 采纳 | 前端协议约定，Gateway 据此路由到 SessionMgr |
| 3 | 8.2 D4 删除会话 | 调整 | 纯控制请求，三角形路径 Mgr→Data→Adapter；Data fan-out 含源端确认 |
| 4 | 8.3 D5 client 断连 | 调整 | 先走回路再清本地状态，与手动登出统一 |
| 5 | 7.4.1/7.4.4 手动登出 | 调整 | 同样改为先走回路再清 userToConn_ |
| 6 | 8.4 消息同步 | 采纳 | 引用 Round 5/6；上下文同步直接回源不走 fan-out |

#### N-2. 否决/调整的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| 独立 NewSessionReq + 单独 fan-out 通知 | AiChat 场景无独立建 session 请求，总是捆绑首条数据 | 捆绑请求，ACK fan-out 即通知 |
| SessionMgr 直接回 Adapter（删除会话） | 通知其他端是 D 面职责，Mgr 只做裁判 | 三角形路径 Mgr→Data→Adapter |
| Adapter 断连时立即清除 userToConn_ | 应先完成内部回路确认再清本地状态 | 先走流程再清 |
| 手动登出立即清 userToConn_ | 与断连登出不一致 | 统一为先走回路再清 |
| 8.3.2 先清后发 | 本轮回滚，改为先发后清 | — |

#### N-3. 对后续轮次的影响

| 轮次 | 影响 |
|------|------|
| Round 9 | 新增"无 GTID = 新 Task"协议约定需纳入 9.5 消息头定稿；错误码新增 NO_GTID_NEW_TASK |
| 7.4 | 已回滚：手动登出 Adapter 侧行为同步调整 |

---

### O. Round 9 迭代记录

> 日期：2026-07-01

#### O-1. 确认项清单

| # | 项 | 结论 | 关键理由 |
|---|-----|------|----------|
| 1 | 9.1 SessionFlags 类设计 | 采纳 | 消灭魔法数字，`SessionFlags::make<AppType>()` 编译期构造，`isNeedAck()` 运行时查询 |
| 2 | 9.2 BatchCounter 超时检测 | 调整 | FlowHub 无定时器，超时由前端负责；资源防漏用分配时 GC |
| 3 | 9.2 BatchCounter index 即 token | 采纳 | `allocate(N)→index`，消息携带 `batchIndex` 往返 |
| 4 | 9.3 错误处理 | 采纳 | `enum class ErrorCode` 放 `common/type/`，当前打 log + 返回前端 |
| 5 | 9.4 常量定稿 | 采纳 | 7 个 MAX_* 常量通过 `.mt` + `gen_code.py` 生成 |
| 6 | 9.5 消息头字段定稿 | 采纳 | 5 字段：uid、gtidList、accessType、appType、sessionFlags |
| 7 | sourceAccessType 移除 | 精简 | `head.accessType` 天然充当源标识（Gateway 不重写 FanOutMsg head） |
| 8 | GTID 新 Task 标识 | 调整 | 序列号位全 1 哨兵值（非空 GTID），需更新 GTID 编码定义 |
| 9 | gen_code.py 适配 | 待实现 | `.mt` 支持 `include "common/SessionFlags.hpp"`，文件不存在时报错 |

#### O-2. 否决/调整的方案

| 方案 | 否决/调整原因 | 替代 |
|------|--------------|------|
| sessionFlags 裸 uint8_t | 魔法数字散落，可读性差 | `SessionFlags` 类封装 |
| BatchCounter 定时器超时 | FlowHub 当前无定时器机制 | 前端自维护超时 + 分配时 GC 防泄漏 |
| sourceAccessType 独立字段 | Gateway 不重写 head.accessType，源标识冗余 | 移除，用 `head.accessType` |
| 空 GTID 标识新 Task | 丢失 TaskType 信息 | 特殊 GTID（seq 全 1） |
| 常量手写 Constants.hpp | 与 AccessType/AppType 管理方式不一致 | `.mt` + 脚本生成 |

#### O-3. 阶段 D 收尾

> 全部 9 轮确认完毕。以下为收尾建议。

**适合提炼为独立 ADR 的决策**：
- `SessionFlags` 类设计（ADR-xxxx：编译期 flag 映射模式）
- `BatchFanOut/FanOutMsg` 两级 fan-out 机制（ADR-xxxx）
- 捆绑请求 + GTID 哨兵值（ADR-xxxx：新建 Task 协议）
- `sourceAccessType` 移除与 `head.accessType` 复用（ADR-xxxx）

**留到实现阶段再定**：
- BatchCounter 超时机制的定时器方案（当前前端负责）
- 错误处理的详细策略（当前打 log + 返回错误码）
- context 持久化/加载（登出归零存盘、登录冷启加载——当前预留）
- Adapter 基类设计（WebBase 等共享代码抽取）


