# ADR-0021：SessionFlags 编译期 flag 映射模式

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-01 | — |

---

## 背景

AppType 描述了客户端的功能集合（"做什么"——AiChat、SmartHome 等）。不同 AppType 对消息处理有不同的行为要求：
- AiChat 类 App 需要多端消息同步，每条请求都要求 Business 层回复 ACK
- CLI AiChat 是单端接入，不需要 ACK
- SmartHome 类 App 不需要 ACK

我们不希望 Business 层（Business EO）去判断 AppType 来区分行为——这会把上层概念泄漏到业务逻辑层。同时，Adapter 编译期就知道自身的 AppType，可以在发出消息时把 AppType 特有的要求编码进消息标志位。

需要一个类型安全、零运行时开销的机制，让 Adapter 编译期将 AppType 映射为行为标志，Business EO 运行时读取标志执行对应逻辑。

## 决策

### 核心设计：`SessionFlags` 值类型类

```cpp
class SessionFlags {
public:
    enum class BitFlags : uint8_t {
        needAckBit = 0x01,
    };

    // 默认构造：用于构造空消息，flags_ = 0
    constexpr SessionFlags() : flags_(0) {}

    // 静态工厂：模板参数为 AppType，编译期完成映射
    template <AppType AT>
    static constexpr SessionFlags make() {
        uint8_t v = 0;
        switch (AT) {
            case AppType::AiChat:
                v = static_cast<uint8_t>(BitFlags::needAckBit);
                break;
            default:
                break;
        }
        return SessionFlags{v};
    }

    // 查询接口：运行时一条 test 指令
    constexpr bool isNeedAck() const {
        return flags_ & static_cast<uint8_t>(BitFlags::needAckBit);
    }

private:
    uint8_t flags_;

    // 私有值构造函数：仅供 make() 内部调用，禁止外部裸 uint8_t 构造
    explicit constexpr SessionFlags(uint8_t v) : flags_(v) {}
};
```

### 关键设计决策

1. **编译期映射**：`SessionFlags::make<AppType>()` 内部 `switch (AT)` 在编译期常量折叠，零指令开销。新增 AppType 只需在 switch 中加 `case`，老代码不碰。

2. **私有值构造函数**：禁止外部用裸 `uint8_t` 构造 `SessionFlags`，防止魔法数字绕过工厂方法。`make<>()` 是唯一入口。

3. **运行时查询零开销**：`isNeedAck()` 编译为单条 `test` 指令。Business EO 无需感知 AppType。

4. **消灭魔法数字**：不再出现 `if (flags & 0x01)` 或 `flags |= NEED_ACK` 等散落各处的位运算。

### 使用方式

```cpp
// Adapter 编译期定义自身 AppType
static constexpr AppType MyAppType = AppType::AiChat;

// Adapter 构造消息（通用写法，不硬编码 AppType 值）
RequestHead head;
head.sessionFlags = SessionFlags::make<MyAppType>();

// BusinessEO 读取
if (msg.head.sessionFlags.isNeedAck()) {
    sendAck();
}
```

### 当前映射表

| AppType | needAckBit |
|---------|-----------|
| AiChat | 1 |
| 其他 | 0 |

### 代码位置

- 类定义：`common/SessionFlags.hpp`
- `gen_code.py` 需适配 `.mt` 文件中 `include "common/SessionFlags.hpp"` 语法

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 裸 `uint8_t` 位运算 | 魔法数字散落各处（`flags \|= 0x01`、`if (flags & 0x01)`），可读性差，新增 flag 时需全局搜索 |
| 运行时查表（`map<AppType, SessionFlags>`） | 引入不必要的运行时开销；AppType→flags 映射是静态知识，运行时查表是浪费 |
| Business EO 直接判断 AppType | 违反分层原则——业务层不应感知接入层概念 |
| 每个 flag 独立字段（`bool needAck; bool needSync; ...`） | 消息头字段膨胀；不如位压缩紧凑 |

## 影响

- **Adapter**：编译期调用 `SessionFlags::make<MyAppType>()` 构造标志，填入消息头。开发者不会遗漏——编译期常量，不填就是编译错误。
- **Business EO**：只读 `sessionFlags.isNeedAck()` 等查询接口，不感知 AppType。行为逻辑集中在标志位判断。
- **消息头**：SessionFlags 替代裸 `uint8_t` 成为消息头字段类型。
- **gen_code.py**：需新增对 `include "common/SessionFlags.hpp"` 的识别与生成。
- **扩展性**：新增 flag 时在 `BitFlags` 枚举加值、在 `make<>()` 的 switch 中加映射、加查询方法——三处改动，集中在一份文件。
