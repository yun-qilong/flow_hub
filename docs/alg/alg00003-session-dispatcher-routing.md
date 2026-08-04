# alg00003：SessionDispatcher Routing

| 属性 | 值 |
|------|-----|
| 编号 | alg00003 |
| 对应 Subfeature | FT0002-A, FT0002-B |

---

SessionDispatcher 从 `head.sessionTaskId` 高位提取 TaskType，以此作为下标查找编排器地址表，将消息委托给对应的编排器。SessionDispatcher 只处理入向路由，出向由编排器直发。
