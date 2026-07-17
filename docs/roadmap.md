# 路线图

> 后续功能规划。排列顺序即大致优先级。

---

## 1. 接入 Home Assistant

通过 MQTT Adapter 对接 Home Assistant / 米家生态。这是架构的第一次跨协议验证——新增一个协议 Adapter，已有代码不动。

涉及：`MqttAdapter` 开发、`ServiceMgr` 设备注册、设备消息格式定义。

## 2. Session 层编排器

接入 Home Assistant 后，智能家居业务不可能塞进一个 EO——不同任务类型的逻辑差异巨大，单一 EO 违反开闭原则，且框架限定同一类型的 Task 在同一线程执行，单一 EO 会成为性能瓶颈。将业务按任务特性拆分到多个 EO 中是必然的。

但拆分之后，同一业务域的 Task（灯光控制、传感器读取、场景切换）之间存在协作关系。如果让这些 EO 在 Business 层直接互相通信来协调，耦合会迅速蔓延。因此在 Session 层 D 面引入**按业务域的编排器**——SessionData 继续负责独立 Task 的消息中转，需要多 Task 协作的业务域由各自的编排器统一调度。

设计尚未定型，计划在接入 Home Assistant 之后、业务逻辑进一步复杂之前完成。

## 3. 多 AI 讨论前端

为多 AI 协同讨论增加 Web 前端，脱离当前 CLI 终端交互模式。

涉及：`WsAdapter` 开发、前端页面。

## 4. C 面生命周期管理

完善各层 C 面 Mgr 对 D 面 EO 的生命周期管理。当前 EO 在 `main()` 中创建后一直存活，不存在启动/停止/替换流程。

涉及：SessionMgr 管理 SessionData、BusinessMgr 管理 Router 与业务 EO、ServiceMgr 管理 ServiceGateway 与 Adapter。完成后 D 面 EO 崩溃可由 C 面感知并重启/替换。

## 5. 消息规范化

接入 Home Assistant 之后，对现有全部消息类型做一次统一审查：固化字段、统一命名、建立新消息的参考范式。

## 远期

- 跨生态设备编排（米家传感器 → HomeKit 动作）
- 异构算力调度（ARM 常驻 + GPU 按需）
- 框架抽象层落地（切换 CAF → 其他运行时）
