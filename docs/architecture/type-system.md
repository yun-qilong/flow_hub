# 类型体系

> Flow Hub 定义三种正交类型来标识"怎么连"、"做什么"、"哪个任务"。

---

## 一、设计原则

三层类型体系的核心思想：**每层只知道自己需要知道的**。

- Access Adapter 知道用户通过什么方式连进来（AccessType）和用的什么前端（AppType）
- SessionMgr 知道用户是谁、有权限做什么（AppType → TaskType 集合）
- Business EO 只知道要执行什么任务（TaskType），不需要知道用户怎么连的、用的什么前端

这种信息隐藏通过编译期机制保证：AppType 在 Business 层不可见；AppType 特有的行为差异由 Adapter 编译期编码进 `sessionFlags`，Business EO 只读标志位行事。

---

## 二、AccessType

**定位**：Adapter 唯一标识。每个 (AppType × 连接方式) 组合对应一个 AccessType 值。

**示例**：
| AccessType | AppType | 连接方式 |
|-----------|---------|---------|
| `AiChatWeb` | AiChat | WebSocket |
| `AiChatCLI` | AiChat | stdin/stdout |
| `SmartHomeWeb` | SmartHome | WebSocket |
| `SmartHomeApp` | SmartHome | TCP |

**实际使用者**：
- Adapter：填入消息头 `accessType`（编译期常量）
- Gateway：查 `adapterTable_[accessType]` 做回程路由
- SessionData：`userAccessBitset[uid]` 中对应位标记用户当前活跃的接入方式；上行 fan-out 时直接拷贝为 `targets` 位图

**定义位置**：`src/common/type/AccessType.mt`（枚举），由 `gen_code.py` 生成 C++ 代码。

---

## 三、AppType

**定位**：客户端功能集合——"做什么"。描述前端的功能类别。

**示例**：`AiChat`、`SmartHome`、`IndustrialControl`

**关键约束**：
- **AppType 止于 Session 层**——Business 层及以下不可见 AppType
- AppType 特有的行为要求（如 AiChat 需要多端消息同步）由 Adapter 编译期编码进 `sessionFlags`
- 每个 AppType 包含一组正交 TaskType（编译期常量），SessionMgr 据此做 `NewTask` 闸门校验

**定义位置**：`src/common/type/AppType.mt`（枚举），由 `gen_code.py` 生成。

---

## 四、TaskType

**定位**：原子业务任务——"哪个任务"。全链路可见（编码在 GTID 中）。

**编码方式**：
```
TaskType value = (Category << 6) | subType

Category: 0x0 (System) / 0x7 (User) / 0xC (Other)
subType:  各 Category 内从 0 起的连续编号
```

**示例**：
| 枚举值 | 值 | 含义 |
|--------|-----|------|
| `TaskType::Service` | `0x0000` | 系统运维 |
| `TaskType::AiChat` | `0x01C0` | 单 AI 对话 |
| `TaskType::Session` | `0x0300` | 会话管理 |

此布局使得 `extractTaskType(gtid) = gtid >> 6`——Router 查表极简。

**关键约束**：
- **无 super TaskType**——TaskType 正交，不存在组合多个 TaskType 的新 TaskType。融合业务 = 新 TaskType + 新 EO（调用公共工具类）
- TaskType 空间最大 1024（10-bit），实际启用 ≤ 192

**定义位置**：`src/common/type/TaskType.mt`，由 `gen_code.py` 生成。

---

## 五、类型可见性矩阵

| | AccessType | AppType | TaskType |
|---|---|---|---|
| 前端 | ❌ 不感知 | ✅ 声明 | ✅ 每个 Task 都有 |
| Adapter | ✅ 编译期填入 | ✅ 编译期据此组织消息 | ✅ 透传（GTID 中） |
| Gateway | ✅ 查表转发 | ❌ 不感知 | ❌ 不感知 |
| SessionMgr | ✅ 回程用 | ✅ 拼 username key | ✅ 分配 GTID |
| SessionData | ✅ fan-out 位图 | ✅ 初始动作决策 | ✅ delegate |
| Router | ❌ 透传 | ❌ 不可见 | ✅ 按 TaskType 路由 |
| Business EO | ❌ 透传 | ❌ 不可见 | ✅ 执行 |
| Service | ❌ 透传 | ❌ 不可见 | ❌ 透传 |

---

## 六、uid 编码

```
uid = uint16_t，编码 [userId:8][AppType:8]

MAX_USERS  = 64（userId 上限）
MAX_APP_TYPES = 64
MAX_UID    = 65536（全 16 位空间）
```

实际有效 uid：64 × 64 = 4096 个。

**不同层的索引策略**：
- **SessionData**：`userAccessBitset` 是 `uint64_t[65536]`（512 KB），直接用 uid 下标。fan-out 是高频操作，省 `[userId][appType]` 解码换零翻译
- **SessionMgr**：`UserRecord[64][64]` 二维表。每项包含 name + GTID 列表，flat 65536 项会浪费 ~2 GB

同一真实用户在不同 AppType 下：相同 username → 相同 userId（高 8 位），不同 AppType（低 8 位）→ 不同 uid。fan-out 自动隔离。

---

## 七、SessionFlags 编译期映射

`SessionFlags` 是将 AppType 特有行为编码为位掩码的机制：

```cpp
class SessionFlags {
    enum BitFlags : uint8_t { needAckBit = 0x01 };

    template <AppType AT>
    static constexpr SessionFlags make() {
        uint8_t v = 0;
        switch (AT) {
            case AppType::AiChat:  v = needAckBit; break;
            default:               break;
        }
        return SessionFlags{v};
    }

    constexpr bool isNeedAck() const { return flags_ & needAckBit; }
};
```

- Adapter 编译期调用 `SessionFlags::make<kAppType>()`（零运行时开销）
- Business EO 运行时调用 `head.sessionFlags.isNeedAck()`（一条 `test` 指令）
- 新增 AppType 只需在 `make<>()` 的 switch 中加映射分支

详见 [ADR-0021](../adr/0021-session-flags-compile-time.md)
