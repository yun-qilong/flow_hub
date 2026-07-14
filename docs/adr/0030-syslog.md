# ADR-0030：SysLog — 编译期过滤、配置驱动的日志系统

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-14 | — |

---

## 背景

项目中散落 `std::cout` / `std::cerr` 打印，无等级、无过滤、无法在 UT 中验证。需要一个统一的日志系统替代。

---

## 决策

### 1. 日志等级

```cpp
enum class LogLevel : uint8_t { ERR = 0, WRN = 1, INFO = 2, DBG = 3 };
```

- 严重程度：`ERR > WRN > INFO > DBG`（枚举值越小越严重）
- 运行时阈值 `kRuntimeMinLevel`：低于该等级的日志不输出
- 编译期过滤：`kRuntimeMinLevel` 是 `constexpr`，`if constexpr` 消除死代码

### 2. Feature 日志

```cpp
enum class LogFeature : uint8_t { AICHAT = 0, AIDISCUSS = 1 };
```

- Feature 日志通过 `LG_FEAT(feat, fmt, ...)` 宏调用
- 每个 Feature 独立开关，配置文件中白名单控制
- 未列入白名单的 Feature 在编译期完全消除

### 3. 配置文件 → 脚本 → 生成头文件

```
config/log_config.conf          ← 开发者编辑（文本）
       │
       ▼
scripts/gen_log_config.py       ← 解析 + 校验 + 生成
       │
       ▼
src/generated/LogConfig.hpp     ← constexpr 常量，编译进二进制
```

配置文件语法：
```ini
log_dir     = logs
max_size_kb = 1024
log_level   = INFO          # ERR | WRN | INFO | DBG

# Feature 白名单（只写名字即开启，未列出则关闭）
AICHAT
```

脚本职责：
- 解析配置文件，校验所有值合法性
- 读取 `LogTypes.hpp` 获取合法 Feature 名，校验白名单
- 出错时打印精确错误信息（行号、错误值、正确填法），并**自动恢复出厂配置**
- `--restore` 可手动恢复配置为出厂状态

### 4. SysLog 类

```cpp
class SysLog
{
  public:
    explicit SysLog(const std::string &logDir, uint32_t maxSizeKb);
    void log(LogLevel level, const std::string &msg);
    void logFeature(LogFeature feature, const std::string &msg);
};
```

- 具体类，无继承。Mock 模式下被 `MockSysLog`（独立类，无继承）整体替换
- 输出到 `logs/` 目录，文件名 `flowhub_YYYYMMDD_HHMMSS.log`
- 单文件最大 `max_size_kb` KB（默认 1024 = 1 MB），超限自动轮转（`.log` → `.log.1`）
- 前缀格式：`[LEVEL] [HH:MM:SS.mmm] message`
- 线程安全（`std::mutex`）
- 全局实例指针 `gSysLog`：生产设 `SysLog`，UT 设 `MockSysLog`

### 5. 日志宏

```cpp
LG_ERR("register failed: code=%d", code);
LG_WRN("timeout after %d ms", ms);
LG_INFO("user %s logged in", username);
LG_DBG("gtid=0x%x, count=%d", gtid, n);
LG_FEAT(AICHAT, "chat session created: uid=%d", uid);
```

- printf 风格。编译期过滤和空指针检查**全部封装在模板函数内**，宏展开仅为一次函数调用：
  ```cpp
  // LG_ERR("x %d", n) 展开为：
  ::utils::doLog<::utils::LogLevel::ERR>(::utils::formatLog("x %d", n))
  ```
- 调用处**零认知复杂度**：`if constexpr` 和 `if (gSysLog)` 在 `doLog<T>` 模板内部，不计入调用函数
- 性能无代价：`inline` 模板 + 编译期常量 → 内联后与手写 `if constexpr` 生成相同汇编

### 6. UT 基础设施

#### EoTestBase 自动初始化

`EoTestBase` 构造函数中自动创建 `StrictMock<MockSysLog>` 并注册到 `gSysLog()`：

```cpp
class EoTestBase : public ::testing::Test
{
protected:
    testing::StrictMock<utils::MockSysLog> mockSysLog_;

    EoTestBase() : stubEo_(env_.system())
    {
        utils::gSysLog() = &mockSysLog_;
    }
};
```

- 所有 EO UT 自动获得 MockSysLog，无需手动初始化
- **StrictMock**：未写 `EXPECT_CALL` 的日志调用会导致测试 FAIL，确保每条日志都被测试覆盖

#### EXPECT_LOG 宏

在 `SysLogMock.hpp` 中提供便捷宏，隐藏 `::testing::_` 和 `.Times()` 样板：

```cpp
#define EXPECT_LOG(level, times)                                                                   \
    EXPECT_CALL(*::utils::gSysLog(), log((level), ::testing::_)).Times(times)

#define EXPECT_LOG_FEAT(feat, times)                                                               \
    EXPECT_CALL(*::utils::gSysLog(), logFeature((feat), ::testing::_)).Times(times)
```

UT 中的使用：

```cpp
TEST_F(TestRouter, CheckRouteAndForward_EmptyGtidList)
{
    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatServiceResp resp;
    fillHead(resp, {});
    sendToMe(std::move(resp));
}
```

- 只验证日志等级 + 调用次数，不验证消息内容
- 无 `mockSysLog_`、`::testing::_`、`.Times()` 样板
- 关闭的等级：`doLog` 函数体为空 → 内联后零指令
- Feature 过滤：`doLogFeat<F>` 不在白名单 → 函数体为空 → 零指令

### 6. Mock 与 UT

遵循 ADR-0029 模式 B（无 empty，默认 mock）：

```
SysLog.hpp 路由块：
  #if !USE_ORIG(SysLog)  →  MockSysLog（默认）
  #else                   →  SysLog（生产）
```

Mock 是独立类，不继承 SysLog：
```cpp
class MockSysLog
{
  public:
    MOCK_METHOD(void, log, (LogLevel, const std::string &));
    MOCK_METHOD(void, logFeature, (LogFeature, const std::string &));
};
```

UT 中不写任何 `#define`，默认获得 mock。验证方式：
```cpp
EXPECT_CALL(*mockLog_, log(LogLevel::ERR, _)).Times(2);   // 只追踪次数
EXPECT_CALL(*mockLog_, log(LogLevel::INFO, _)).Times(1);  // 不验证内容
```

### 7. 文件组织

| 文件 | 作用 |
|------|------|
| `config/log_config.conf` | 用户编辑的配置文件 |
| `scripts/gen_log_config.py` | 解析配置 → 生成头文件 |
| `src/generated/LogConfig.hpp` | 生成的 constexpr 常量 |
| `src/utils/LogTypes.hpp` | LogLevel + LogFeature 枚举 |
| `src/utils/SysLog.hpp` | SysLog 类 + 路由块 + 宏 |
| `src/utils/SysLog.cpp` | SysLog 实现（写文件、轮转） |
| `src/utils/ut/mocks/SysLogMock.hpp` | MockSysLog 独立 mock |

---

## 设计取舍

| 取舍项 | 决策 | 理由 |
|--------|------|------|
| 编译期过滤而非运行时 | `constexpr` + `if constexpr` | 关闭的日志零指令、零二进制膨胀 |
| 宏零认知复杂度 | `doLog<T>` 模板封装所有分支 | 调用处展开为单个函数调用 |
| 配置文件文本格式 | `KEY = VALUE` + 白名单 | 非程序员可编辑，脚本严格校验防手误 |
| 脚本内置出厂配置 | 出错自动恢复 + `--restore` | 不依赖 git 即可恢复，零门槛 |
| 宏而非函数 | `LG_ERR(...)` | printf 风格，调用方简洁；内部做 format → string |
| SysLog 无继承 | 具体类，mock 整体替换 | 符合 ADR-0029；无谓的 base/impl 分层 |
| UT 只验证次数 | `EXPECT_CALL(..., _).Times(N)` | 日志内容不稳定（含时间戳），次数才是契约 |
| 全局指针注入 | `extern SysLog *gSysLog` | 生产设实例，UT 设 mock，无需改业务代码 |
