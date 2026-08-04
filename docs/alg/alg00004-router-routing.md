# alg00004：Router Routing

| 属性 | 值 |
|------|-----|
| 编号 | alg00004 |
| 对应 Subfeature | FT0002-A, FT0002-B |

---

Router 遍历 `head.busTaskIds`，对其中每个 GTID 从高位提取 TaskType 作为下标查找路由表，将消息委托给对应的 Bus 层 EO。委托前将消息中的 `busTaskIds` 替换为仅含当前 GTID 的单元素列表，使 Bus 层 EO 始终看到长度为一的 `busTaskIds`，直接取首个元素即可确定自己要处理的 Task。

例：入向消息 `busTaskIds = [0x9001, 0x9002, 0x9003]`，Router 发出三条消息：

| 发往 | `busTaskIds` |
|------|------|
| 第一条 | `[0x9001]` |
| 第二条 | `[0x9002]` |
| 第三条 | `[0x9003]` |

单 GTID 时上述流程只执行一次，多 GTID 时即为 fan-out。Router 只处理入向路由，出向由 Bus 层 EO 直发。
