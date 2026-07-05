# 文档债务

> 根 README 已根据 `access-session-design-checklist.md` 更新完毕。
> 以下为日后重构文档时需补充的内容，不阻塞当前代码实现。

---

## 待补充的独立架构文档

以下文档建议放入 `docs/architecture/`，从 README 中拆分展开：

- [ ] **type-system.md** — AccessType / AppType / TaskType 完整定义、可见性矩阵、uid 编码设计权衡
- [ ] **data-structures.md** — 各层静态表结构、常量、UserHead 字段详述
- [ ] **message-flow.md** — 下行/上行路径完整序列图 + 各节点行为
- [ ] **fan-out-mechanism.md** — BatchFanOut/FanOutMsg 两级格式、三级职责链、源排除策略
- [ ] **c-plane-lifecycle.md** — 注册/登录/登出/注销完整流程
- [ ] **d-plane-lifecycle.md** — 新建/删除会话、断连、消息同步
- [ ] **session-flags.md** — SessionFlags 类设计、编译期映射模式

## 待补充的 ADR

- [ ] **ADR-0013 superseded 标注**：ADR-0013（Gateway 出向预埋）仍适用于 Service 层下行 fan-out，但已不再用于上行广播（已由 ADR-0022 承担）。建议在 ADR-0013 中加注。
- [ ] **Business/Service 层 ADR**：当前 ADR 偏重 Access-Session 层，Business/Service 层决策记录较零散。

## 待改进的图示

- [ ] 架构总览图（§4）目前为 ASCII 组件图，日后可用 draw.io 等工具绘制更直观的分层架构图
- [ ] 会话生命周期（§6.4）各流程目前为 ASCII 路径图，可转为 mermaid 序列图
- [ ] 上行 fan-out 路径（BatchFanOut → Gateway → FanOutMsg → Adapter）可独立绘制一幅序列图

## 待实现（demo 阶段跳过）

- [ ] **SessionData `batchCounterResources_`（路线图 7.2）**：上下文同步机制。`UserLogin` 时若 gtids 非空，通过 BatchCounter 组装批量消息发往 Router。当前单 Adapter demo 不需要，多 Adapter 场景时实现。

## 待补充的 Business/Service 层细节

- [ ] BusinessMgr 的资源分配策略
- [ ] ServiceMgr 的设备发现与注册协议
- [ ] AutomationBus 规则引擎的具体规则格式
- [ ] DataManager 冷热切换和老化策略
- [ ] 各层错误处理详细策略（当前仅打 log + 返回错误码）

## 远景

- [ ] context 持久化/加载（登出归零存盘、登录冷启加载——当前预留）
- [ ] 消息进静态内存池
- [ ] BatchCounter 超时机制的定时器方案

---

## 当前实现待办

> 2026-07-03，路线图 Step 2 完成后记录。

- [ ] **gen_code.py 支持 `optional` 字段语法**：`.mt` 中 `optional<T>` → 生成 `std::optional<T>`。当前用空 `vector` 代表"无"（如 `UserLoginResp.gtids`），`optional` 语义更清晰。
  - 涉及：路线图 2.10（已搁置）
- [ ] **消息体系文档**：需要一篇独立文档介绍所有 34 个消息类型——各自的作用、字段含义、在完整流程中的位置、带 mermaid 序列图的消息流转全景。
  - 建议位置：`docs/architecture/message-catalog.md`
  - 包含：C 面消息（Register/Login/Logout/Delete ×4 组）、C→D 通知（Session ×4 组）、Task 生命周期（Create/Delete/Sync）、数据面消息（AiChat 系列）、fan-out 机制（UserHead.targets）
