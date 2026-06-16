# ADR-0012：取消 ProtocolGateway，统一为 ServiceGateway

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-16 | 韵启龙 |

---

## 背景

原设计（README v0.3）在 Service Layer D 面设有两个平级入口：

| 入口 | 对接设备 | 职责 |
|------|---------|------|
| **ServiceGateway** | 智能设备（MQTT、AI API 等） | 接收已规范化的消息，fan-out 给 Router |
| **ProtocolGateway** | 傻瓜设备（CAN、Modbus 等） | 过滤垃圾数据 → 规范化 → fan-out 给 Router |

二者的唯一区别是 ProtocolGateway 多一步"过滤垃圾数据 + 规范化"。

设计初衷基于一个假设：CAN 总线、Modbus 等工业总线上的设备一旦连接就会不受控地持续滥发原始数据，需要系统侧主动过滤。ProtocolGateway 的职责就是管住这些"没有自我管理能力的傻瓜设备"。

---

## 决策

**取消 ProtocolGateway。所有外部设备统一经由 ServiceGateway 进入系统。**

理由：

1. **"傻瓜设备"假设不成立**。工业总线上的设备同样是周期性上报或请求-响应模式，并非不受控滥发。CAN 节点按预设周期发送、Modbus 是从机响应主机轮询——都是有秩序的通信，不存在"需要系统主动过滤垃圾数据"的场景。

2. **协议差异是 Adapter 的事，不是 Gateway 的事**。CAN 帧解析、Modbus 寄存器映射等协议层面的工作应由对应 Adapter 内部完成，Adapter 向上交付的已经是规范化消息。Gateway 不需要、也不应该感知协议细节。

3. **单一入口简化架构**。"按设备自治能力分流"的二分法增加了概念负担（两个 Gateway 平级、入口选择原则、适配器不知自己对接哪种入口），而实际收益为零。

---

## 影响

- 文档中所有 ProtocolGateway 引用删除（README v0.3 → v0.4）
- 架构图简化：Service Layer D 面只剩 ServiceGateway 一个入口
- 路由规则简化：`Adapter → ServiceGateway/ProtocolGateway` → `Adapter → ServiceGateway`
- 命名规范中移除 `ProtocolGateway`
- 关键设计决策中两条（"按设备自治能力分流"、"ServiceGateway 与 ProtocolGateway 平级"）合并为一条（"Service 层统一入口 ServiceGateway"）
- 设备数据流从双路径（智能设备 + 傻瓜设备）简化为单一路径
- 前期代码中不存在 ProtocolGateway 实现，无代码修改

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 保留 ProtocolGateway 但标注"预留" | 前提假设错误，标注预留意味着未来也不会用到——不如删除 |
| 将过滤/规范化职责并入 ServiceGateway | 无此需求，增加 Gateway 复杂度且破坏其"只做注册表+fan-out"的单一职责 |
| 不做任何修改 | 文档中存在基于错误假设的设计，误导后续开发 |

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-16 | 初稿，采纳 |
