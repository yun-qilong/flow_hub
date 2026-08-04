# ADR-0031：TaskConfigReq 配置字段采用 JSON 载荷

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 提议中 | 2026-07-30 | 韵启龙 |

---

## 背景

`TaskConfigReq` 是前端发给 FlowHub 编排器的配置消息。当前 FT0002-B 的配置内容包括 `aiCount`、`hasJudge`、`maxRounds`、`maxResponseLength`、`timeoutMs` 以及每个 AI 的 API 参数。这块内容随业务发展会不断变化——不同的 Feature 需要的配置字段完全不同。

---

## 决策

**`TaskConfigReq` 定位为通用消息，不绑定具体业务。配置内容打包为一个 `string payload`（JSON）。**

```mt
message TaskConfigReq
    UserHead head
    string payload
```

`payload` 的结构由业务自行约定。FT0002-B 的 schema 见 [messages 文档](../construct/messages.md#taskconfigreq)。

---

## 为什么这么做

**目标是让 `TaskConfigReq` 像 `TaskCreateReq` 一样——一条消息服务所有业务。**

`TaskCreateReq` 之所以通用，是因为它只携带与业务无关的字段：`taskType` 和 `requestNum`。`TaskConfigReq` 面临的问题不同：不同业务的配置内容天差地别。如果逐字段定义消息类型，每种业务需要自己的 `XxxConfigReq`，消息类型随业务线性增长。

是否值得强搞通用化、通用化会带来多少额外复杂度，这不在 FT0002 的 scope 内。但也不急于现在就下结论说不值得。于是采取了最轻量的方式——`string payload` 包 JSON——既保持了通用性，又不阻塞业务推进。

等未来有了更多业务接入经验后，再重新审视这个问题：如果决定通用化值得，可以升级为模板消息、多态 payload 或其他更适合方案；如果决定不值得，退回到逐字段类型化即可。JSON 方案不预判最终走向，留了最大回旋余地。

---

## 否决的方案

### 逐字段类型化

```mt
message TaskConfigReq
    UserHead head
    uint8 aiCount
    bool hasJudge
    ...
```

这是最干净的做法——编译期校验、字段级访问。但等同于放弃了 `TaskConfigReq` 的通用性。是否放弃通用化是一个需要更多业务经验才能回答的问题，当前不做这个决定。

---

## 影响

- **运行时校验替代编译期校验**。编排器解析 payload 时逐字段 `contains()` 检查，缺失必填项直接回失败。
- **可撤消**。当通用消息的类型安全问题有了更好的解决方案后（模板消息、多态 payload 等），把 JSON 改成结构化字段只是一个局部重构。
- `AiChatConfigReq` 出于同样原因采用 `string payload`。

