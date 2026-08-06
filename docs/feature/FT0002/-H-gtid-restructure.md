# -H：GTID 重构 — Category 体系 + 统一 TaskType

| 状态 | 日期 |
|------|------|
| 设计中 | 2026-07-24 |

> 父级：[FT0002 AI Agora 总览](./README.md)

---

## 目标

建立 System / Session / Bus 三分类 GTID 体系，统一 `TaskType` 枚举。重命名 `AiChat` → `AiAgora`。为 -A 和后续 subfeature 提供类型基础。

**完成标志**：编译通过，UT 全绿，现有 AiChat CLI 行为完全不变。

---

## GTID Category 划分

| Category | 值 | 名称 | GTID 范围 | 路由层 |
|----------|------|------|-----------|--------|
| `0x0` | System | 系统运维 | `0x0001`~`0x0FFF` | — |
| `0x7` | Session | 用户发起 | `0x7000`~`0x7FFF` | SessionDispatcher |
| `0x9` | Bus | 编排子任务 | `0x9000`~`0x9FFF` | Router |
| 其余 | — | 保留 | — | — |

## TaskType 枚举（统一）

| 枚举值 | 值 | Category | 用途 |
|--------|-----|----------|------|
| `Service` | `(0x0<<6)\|0` | System | 系统运维（不动） |
| `AiAgora` | `(0x7<<6)\|0` | Session | 用户 AI 讨论（was `AiChat`） |
| `AiChat` | `(0x9<<6)\|0` | Bus | AI 子对话（参辩/裁判共用，`AiIndex` 区分身份） |


所有值由 `gen_code.py` 从 context 目录名（Category）和文件名（subType 序号）自动计算。

---

## 不改的

- 消息类型名（AiChatBusinessReq 等）
- AiChatBus 类名（模板参数仍为 `TaskType`）
- Router 路由逻辑
- Bus Category 枚举值仅定义不使用

---

## 实现概要

**gen_code.py**：`CATEGORY_DIRS` 改为三目录（`sessionContext`、`busContext`），生成统一 `TaskType.hpp`。

**Context 目录**：

| 操作 | 旧 | 新 |
|------|-----|-----|
| 移动 | `userContext/AiChatContext.mt` | `sessionContext/AiAgoraContext.mt` |
| 新建 | — | `busContext/AiChatContext.mt`（占位，参辩/裁判共用） |
| 删除 | `otherContext/SessionContext.mt` | — |

**类型 .mt**：`AppType::AiChat` → `AiAgora`，`AccessType::AiChatCLI` → `AiAgoraCLI`。

**源文件**：所有 `TaskType::AiChat` → `TaskType::AiAgora` 引用更新。涉及：`SessionMgr`、`CliAdapter`、`AccessGateway`、`Router`、`AiChatBus`、`main`、`ServiceGateway`。

**ADR-0008**：三分类表，删除 Other Category。

---

## 相关文档

| 文档 | 说明 |
|------|------|
| [ADR-0008：GTID](../adr/0008-gtid.md) | GTID 编码规范 |
| [gen_code.py](../../scripts/gen_code.py) | 代码生成脚本 |
