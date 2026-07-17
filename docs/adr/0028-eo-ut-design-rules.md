# ADR-0028：EO 类 UT 设计规范与通用写法

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-07-11 | 韵启龙 |

---

## 背景

项目需要为所有 EO 类（继承 `fw::EoBase` 的 CAF actor）建立可复用的 UT 框架。经过 `SessionData`、`AccessGateway`、`BusinessMgr`、`Router`、`ServiceGateway`、`ServiceMgr`、`AiChatBus`、`AiApiAdapter` 共 8 个类的 UT 实践，已形成稳定的模式与规范。本文档将这些经验固化为 ADR。

---

## 决策

### 1. EO 类 UT 基本原则

EO 类仅被 CAF 框架通过消息驱动，不被其他 C++ 代码直接 include 或实例化。因此 UT **不修改被测头文件**——通过 `EoTestBase` 提供的 stub 原语验证消息收发即可，无需 mock。

### 2. EoTestBase — 零 CAF 痕迹

`fw::EoTestBase`（继承 `::testing::Test`）包裹所有 CAF 原语，测试文件中**严禁出现裸 `caf::` 调用**。

| EoTestBase 类型/方法 | 隐藏的 CAF 原语 |
|---------------------|-----------------|
| `Stub` | `caf::scoped_actor` |
| `Actor` | `caf::actor` |
| `makeStub()` | `make_unique<caf::scoped_actor>(system())` |
| `stubAddress(s)` | `caf::actor_cast<caf::actor>(s->address())` |
| `spawn<T>(args...)` | `system().spawn<T>(args...)` |
| `sendToMe(msg)` | `stubEo_->mail(msg).send(testee_)` |
| `sendToMeFrom(sender, target, msg)` | `sender->mail(msg).send(target)` |
| `checkOutput<T>(stub, lambda)` | `stub->receive([&](T &msg) { lambda(msg); })` |
| `checkOutput<T>(lambda)` | 同上，默认从 `stubEo_` 接收 |
| `checkOutputAndReply<Req, Resp>(stub, check, reply)` | `stub->receive` + lambda 返回 reply |
| `trackStub(stub)` | 注册到 `trackedStubs_` 列表 |
| `verifyAllStubsEmpty()` | 遍历 `trackedStubs_`，每个 `receive` + `after(10ms)` 断言空 |
| `stopActor(a)` | `anon_send_exit(a, user_shutdown)` |

**TearDown 自动化**：基类 `TearDown()` 自动调用 `stopActor(testee_)` + `verifyAllStubsEmpty()`。派生类**不得**重写 TearDown（除非有特殊清理需求且调用基类版本）。

### 3. Stub 生命周期管理

```cpp
// ✅ 正确：创建 → track → TearDown 自动清空
downstream_ = makeStub();
trackStub(downstream_);

// ❌ 错误：创建但未 track → 残留消息不会被检测
downstream_ = makeStub();
// 忘记 trackStub(downstream_);
```

**原则**：每个 stub（包括基类内置的 `stubEo_` 自动 tracked）都必须 `trackStub()`，确保 TearDown 能检测到任何未预期的残留消息。

### 4. 消息交互三段式

每个 TEST_F case 遵循标准三段式：

```
sendToMe(msg)                  // 1. 灌入：触发被测 handler
checkOutput<Resp>(verify)      // 2. 验证：handler 的输出（回复或转发）
// TearDown 自动               // 3. 兜底：verifyAllStubsEmpty 确保无意外消息
```

- **回复**（`replyToSender`）：`checkOutput<T>(lambda)` 从 `stubEo_` 接收
- **转发**（`sendTo`/`delegateTo`）：`checkOutput<T>(stub, lambda)` 从指定 stub 接收
- **无输出**：只写 `sendToMe(msg)`，TearDown 自动验证所有 stub 空

### 5. sendToMe vs sendToMeFrom

| 方法 | 使用场景 | 理由 |
|------|---------|------|
| `sendToMe(msg)` | 所有常规 case | sender 身份统一由 `stubEo_` 承担；EO 框架下 handler 不感知 sender |
| `sendToMeFrom(stub, target, msg)` | 仅在 SetUp helper 或 TempConfig 注册时 | 需要特定 sender 身份来设置 `senderAddress()` |

**示例**：
```cpp
// TempConfig handler 用 senderAddress() 存储地址 → 必须用 sendToMeFrom
sendToMeFrom(serviceGatewayStub_, testee_, TempConfig{9});

// 常规业务消息 → 用 sendToMe
sendToMe(std::move(req));
```

### 6. Head 字段填充规则

```cpp
template <typename M>
void fillDefaultHead(M &msg)
{
    msg.head.uid = kTestUid;
    msg.head.accessType = kDefaultAccessType;
    msg.head.appType = kDefaultAppType;
    msg.head.sessionFlags = {};
    msg.head.gtidList.push_back(kTestGtid);
    // 不填 targets — targets 是 handler 的输出字段
}
```

- **必填 5 字段**：uid、accessType、appType、sessionFlags、gtidList
- **不填 targets**：`targets` 是 handler 的计算输出，测试验证 handler 是否正确填充了它
- **gtidList** 按测试需要设置（可单/多/空）

### 7. 构造消息 drain

构造时发出的 `TempConfig` 在 SetUp 中 drain，不设独立 TEST_F：

```cpp
void SetUp() override
{
    // ... 创建 stub、spawn testee_ ...
    checkOutput<TempConfig>(businessMgrStub_, [](TempConfig &msg) {
        EXPECT_EQ(msg.tag, 5);
    });
}
```

**理由**：构造消息的行为单一无分支，drain 即覆盖。

### 8. case 命名与粒度

| 规范 | 格式 | 示例 |
|------|------|------|
| 文件名 | `TestXxx.cpp` | `TestRouter.cpp` |
| 测试类 | `TestXxx` | `TestRouter` |
| case 名 | `CheckHandle<Msg>_<Scenario>` | `CheckHandleAiChatBusinessReq_FullFlow` |

**粒度原则**：每个 case 只验证一个 handler 的一种行为路径。状态准备通过 fixture/helper 完成，不耦合其他 handler。

### 9. CMake 集成

使用 `flowhub_add_ut()` 一行注册：

```cmake
flowhub_add_ut(TestRouter
    SOURCES TestRouter.cpp ../Router.cpp
    EXTRA_LIBS GTest::gmock_main
)
```

该函数自动处理：`FLOWHUB_TEST_BUILD` 宏定义、`gen_code` 依赖、`flowhub_ut` 汇总 target、CTest 注册、`flowhub` 标签。

**EXTRA_LIBS 选择**：
- 纯 EO 转发类（无额外依赖）：`GTest::gmock_main`
- 依赖 common 库（TaskPool、Context 等）：`GTest::gmock_main commonLib`
- 依赖 utils 库（HttpClient、JsonCoDec 等）：`GTest::gmock_main utilsLib`

### 10. CTest 隔离

| 命令 | 过滤方式 | 说明 |
|------|---------|------|
| `ctest -L flowhub` | 标签 | 项目 UT（`LABELS "flowhub"`） |
| `ctest -R "^caf\\."` | 名称正则 | CAF 内部测试 |
| `cmake --build build --target flowhub_ut` | target | 编译项目 UT 不含 CAF |

标签隔离优于名称正则：修改 CAF 版本不影响项目过滤。

### 11. TYPED_TEST 用于行为一致的 handler 组

当多个消息类型的 handler 行为完全一致（同一段代码路径）时，使用 `TYPED_TEST_SUITE` + `TYPED_TEST` 避免重复：

```cpp
using ReqTypes = ::testing::Types<UserRegisterReq, UserLoginReq, ...>;
TYPED_TEST_SUITE(TestXxx_GroupName, ReqTypes);

TYPED_TEST(TestXxx_GroupName, CommonBehavior)
{
    sendToMe(TypeParam{});
    checkOutput<TypeParam>(downstream_, [](auto &) {});
}
```

**条件**：行为 100% 一致（同一 `delegateTo`/`sendTo` 调用，相同条件判断）。有一个 handler 行为不同就独立 TEST_F。

### 12. 依赖外部服务/IO 的 EO 类

如 `AiApiAdapter`（调用 `HttpClient::postJson` 执行 HTTP）：

- 使用无效地址（如 `127.0.0.1:1`）确保快速失败，避免阻塞超时
- 验证消息级行为（resp 是否构造、是否发到正确目标），不验证 HTTP 内容
- 不为此类依赖单独引入 mock 层 — EO 类遵循零头文件改动原则

---

## 设计取舍

| 取舍项 | 决策 | 理由 |
|--------|------|------|
| 不用显式 `checkNoOutput<T>` | TearDown `verifyAllStubsEmpty` 统一兜底 | 自动化优于手动，不遗漏 |
| `fillDefaultHead` 不填 `targets` | targets 是 handler 输出字段 | 职责分离：handler 填 → 测试验 |
| lambda 参数统一为 `msg` | 输出消息变量名统一 | 简洁一致，避免 `fwd`/`resp`/`sync` 分类型命名 |
| 常量用 `constexpr` 提到顶部 | 魔法数字集中管理 | 匿名 namespace 顶部一目了然 |
| 构造消息 drain 在 SetUp | 不设独立 TEST_F | 行为单一无分支 |
| `TaskType` 索引避免用真实枚举值 | 用 0/1/2 等简单索引 | 避免依赖枚举具体值，测试更稳定 |

---

## 关联

- ADR-0018：EoBase 消息转发零拷贝优化（`delegateTo`/`sendTo` 语义）
- ADR-0011：GTID 路由键（Router 路由表索引 `gtid >> 6`）
- `.github/copilot-instructions.md`：项目代码风格规范
