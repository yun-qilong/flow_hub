# ADR-0016：引入 EoEnv 包装层，彻底隐藏 CAF

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-18 | 韵启龙 |

---

## 背景

项目初期为快速验证，`main.cpp` 直接使用 `caf::` 命名空间的 API：

```cpp
caf::init_global_meta_objects<caf::id_block::flowhub>();
caf::actor_system_config cfg;
caf::actor_system sys{cfg};
auto eo = sys.spawn<T>(...);
caf::anon_mail(msg).send(target);
sys.await_all_actors_done();
```

`fw/EoBase.hpp` 和 `fw/EoTypes.hpp` 虽然封装了 actor 定义层，但**实例化方式**（spawn、anon_mail、await_all_actors_done）仍暴露 CAF API。业务代码需要同时理解 `fw::` 和 `caf::` 两套接口，且底层框架替换时 `main.cpp` 和所有测试代码都需修改。

## 决策

### 1. 新增 `EoEnv` 包装类

将 `caf::actor_system` 包装为 `fw::EoEnv`，对外提供项目术语命名的接口：

| 原 CAF 接口 | 包装后接口 | 说明 |
|-------------|-----------|------|
| `caf::actor_system_config` | `fw::EoSystemConfig`（别名） | 已有，保留 |
| `caf::actor_system` | `fw::EoEnv` | 包装类，非别名 |
| `sys.spawn<T>(args...)` | `env.createEo<T>(args...)` | 统一入口。根据 `T::kMayBlock` 自动选择共享池或独立线程 |
| `caf::anon_mail(msg).send(target)` | `fw::anonSendTo(target, msg)` | 自由函数，匿名发送 |
| `sys.await_all_actors_done()` | `env.awaitAllDone()` | 等待所有受管控 EO 结束 |
| `self->current_sender()` | `self->senderAddress()` | 返回当前消息发送者地址（在 `EoBase` 上） |

### 2. `anonSendTo` 设计为自由函数

匿名发送不依赖 `actor_system` 实例（CAF 的 `anon_mail` 也是全局自由函数），因此不挂载在 `EoEnv` 类上，而是作为 `fw::` 命名空间下的独立函数。这与 `EoBase::sendTo`（成员，带发送者身份）形成对比。

### 3. `EoSystem` 别名移除

原先 `using EoSystem = caf::actor_system` 是一层薄别名，不提供任何封装价值，反而让 `sys.` 点出来仍是完整的 CAF API。现以 `EoEnv` 包装类取代。

### 4. 构造时自动初始化 CAF 元对象

`EoEnv` 利用成员声明顺序，在 `actor_system` 构造前自动调用 `caf::init_global_meta_objects<>()`，避免业务代码遗漏。

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 保持 `EoSystem` 别名 + 添加自由函数 | 别名无法阻止业务代码绕过封装直接调 `sys.spawn()` |
| `sendTo` 作为 `EoEnv` 静态方法 | 匿名发送与 `actor_system` 无关，挂在类上语义不自然 |
| 保留独立的 `createDetachedEo` / `createStandaloneEo` | 增加程序员心智负担：需要自行判断用哪个接口。现由 `kMayBlock` 编译期标签（ADR-0017）自动选择 |

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-18 | 初版 |
| 2026-06-21 | 增补 EO 消息 include 约定：init() 内联 .hpp，不 include 消息头 |

## 影响

- `main.cpp` 及所有测试代码中不再出现 `caf::` 命名空间
- 新增 `src/fw/EoEnv.hpp` 文件
- `EoBase` 静态方法 `anonSendTo` 改为委托 `fw::anonSendTo`
- `commonLib` 新增 `CAF::core` 依赖（因 `TaskType` 需要 CAF 序列化支持）

---

## EO 消息 include 约定（2026-06-21 增补）

### 规则

1. **EO 的 `.hpp` 不 `#include` 任何消息头文件**。消息类型定义由 `EoBase.hpp` → `EoEnv.hpp` → `Messages.hpp` 链路间接提供。
2. **`init()` 实现在 `.hpp` 中内联**，以一眼看出该 EO 处理哪些消息：

```cpp
class Foo : public fw::EoBase<Foo>
{
protected:
    void init() override
    {
        onMsg<common::message::MsgA>();
        onMsg<common::message::MsgB>();
    }
};
```

3. EO 的 `.hpp` 只 include 框架头（`fw/EoBase.hpp`）和业务依赖（`TaskPool.hpp`、`HttpClient.hpp` 等），不 include 消息头。

### 理由

- **可读性**：打开 `.hpp` 即看到消息列表，无需跳转 `.cpp`
- **避免冗余**：消息类型通过 `EoEnv.hpp` → `Messages.hpp` 全局可用，显式 include 仅为文档作用
- **维护性**：新增/删除消息处理时只需改 `init()` 一处，无需同步 include

### 不适用 lint 强制

当前项目规模小，EO 数量有限，不作 clang-tidy `include-what-you-use` 强制。
