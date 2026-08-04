# -B：单 AI 路径

| 属性 | 值 |
|------|-----|
| Feature | FT0002 |
| Subfeature | -B |
| 状态 | 设计中 |
| 前置 | -A（SessionDispatcher + 编排器框架） |
| 后置 | -C（Web 前端）、-D（多 AI 辩论） |
| 可测试行为 | 编排器路径完成单 AI 问答，等效现有 AiChat |

## 背景

-A 建立 SessionDispatcher + 编排器（AiDiscussOrchest）框架后，将单 AI 对话迁移到编排器路径：前端经 AccessGateway → SessionDispatcher → 编排器 → Router → AiChatBus → Service（AiApiAdapter），功能等效现有 AiChat，为 -C / -D 铺路。

## 范围与约束

- **仅单 AI**：`aiCount=1`、`hasJudge=false`，无裁判/辩论。多 AI 辩论为 -D scope。
- 编排器按单 AI 模式直通：发送 → 收回答 → 原样回复前端，无轮次/裁判（alg00006 §3.1）。

## 端到端路径

```
CLI(CliAdapter) → AccessGateway → SessionDispatcher → AiAgora（编排器）→ Router → AiChatBus → Service(AiApiAdapter)
```

## 流程与算法文档索引

| 流程 | 算法文档 |
|------|----------|
| 创建会话（TaskCreate） | alg00001、alg00008 |
| 配置（TaskConfig → AiChatConfig） | alg00005、alg00010 |
| 话题问答（AiAgoraChat → AiChat → Service） | alg00006、alg00011 |
| 上下文重置（AiAgoraReset） | alg00009 |
| 删除会话（TaskDelete） | alg00008 |
| CLI 交互（/chat /reset /quit） | alg00012 |
| 路由（AccessGateway / SessionDispatcher / Router） | alg00002、alg00003、alg00004 |
| 消息定义 | `docs/construct/messages.md` |

## 相关文档

- 前端协议：`docs/feature/FT0002/frontend-protocol.md`
- 设计问题：`notes/FT0002-issues.md`（-B Design Issue）

