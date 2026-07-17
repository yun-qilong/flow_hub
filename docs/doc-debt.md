# 技术债

> 已知技术债务，标注优先级和预期偿还时机。

---

## P1 — 接入 Home Assistant 后偿还

### 消息规范化

现有 34 种消息类型在开发过程中逐步增加，命名、字段组织方式不完全一致。接入 Home Assistant 后将产生新的消息类型，在此之前需要对全部消息做一次统一审查：固化字段、建立新消息的参考范式。同步完成 `gen_code.py` 的 `optional<T>` 语法支持。

### 日志增强

当前仅有 SysLog（编译期等级过滤 + Feature 白名单）。缺乏运行时动态等级调整、按 GTID/uid 过滤、统计信息等 debugability 能力。接入 Home Assistant 后业务复杂度上升，需增强日志系统。

---

## P2 — 视需求触发

### C 面生命周期管理

各层 C 面 Mgr 尚未按设计对 D 面 EO 进行生命周期管理。EO 在 `main()` 中创建后一直存活。后续需要：

- SessionMgr 管理 SessionData 的生命周期
- BusinessMgr 管理 Router 及业务 EO 的启停
- ServiceMgr 管理 ServiceGateway 及 Adapter 的注册/注销

涉及热备切换、同类型 EO 负载均衡、消息幂等处理等子问题。该部分设计在 ADR 中已有框架（0019 路由表 Reconfig、0009 Context 与 EO 解耦），等待业务需求驱动实现。

---

## P3 — 远期

### SessionData batchCounterResources_

上下文同步机制——`UserLogin` 时若已有活跃 GTID 则触发。当前单 Adapter 场景不需要，多 Adapter 时实现。

### Context 持久化

登出时 Context 存盘，登录时加载。当前预留接口，未实现。

### 消息进静态内存池

当前消息直接使用 CAF 原生分配。远期消息纳入静态内存池管理。

### BatchCounter 超时机制

上下文同步的 BatchCounter 缺乏超时回收，当前依赖前端超时 + 后端分配时 GC。
