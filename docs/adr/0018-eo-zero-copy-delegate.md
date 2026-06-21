# ADR-0018：EoBase 消息转发零拷贝优化

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-21 | 韵启龙 |

---

## 背景

EoBase 提供 `sendTo`（发送新消息）和 `delegateTo`（转发当前消息）两个接口。底层均通过 CAF `mail(...).send/delegate(target)` 实现。

原有 `onMsg` 注册机制传 `const Msg&` 给 `handle()`，导致即便 handler 调用 `delegateTo(target, req)`，`req` 的 `const` 属性迫使 `mail(const&)` 走拷贝构造——消息体内的 `std::string`、`std::vector` 等堆分配字段深拷贝。对于纯透传的 Gateway/Router，这完全可避免。

---

## 决策

**将 `onMsg` 改造为 move-aware：传 `Msg&` + `std::move`，允许派生类用值传递 handler 接收消息，后续 `delegateTo(target, std::move(req))` 仅移动堆字段指针，避免深拷贝。**

同时 `delegateTo` 增加编译期约束：`static_assert` 禁止传入 const 消息，强制值传递 + move 的用法。

```cpp
// on() wrapper: const Msg& → Msg&（CAF 原生支持）
template <typename Msg, typename F>
void on(F &&handler) {
    auto mh = caf::message_handler{
        [this, h = std::forward<F>(handler)](Msg &m) { h(m); }
    };
    // ...
}

// onMsg(): 参数改 Msg&，调用加 std::move
template <typename Msg>
void onMsg() {
    on<Msg>([this](Msg &msg) {
        this->getImplementation().handle(std::move(msg));
    });
}

// delegateTo: 禁止 const 消息转发
template <typename Msg>
void delegateTo(EoAddress target, Msg &&msg) {
    static_assert(not std::is_const_v<std::remove_reference_t<Msg>>,
                  "delegateTo requires non-const message; "
                  "use value-passing handle(Msg) instead of handle(const Msg&)");
    auto _ = this->mail(std::forward<Msg>(msg)).delegate(target);
}
```

### 派生类用法

```cpp
// 纯透传 EO（如 ServiceGateway）：值传递 + move
void handle(AiChatServiceReq req) {
    delegateTo(adapter, std::move(req));  // 零深拷贝
}

// 纯消费 EO：const& 不受影响（std::move 对 const& 无害）
void handle(const SomeMsg& req) {
    process(req);  // 读字段，不转发
}
```

### 为什么 `sendTo` 不加 `static_assert`

`sendTo` 语义是"发新消息"，调用方构造消息对象，const 与否无关。而 `delegateTo` 语义是"转发当前消息"，const 意味着低效拷贝，应编译期拦截。

---

## 影响

- **EoBase**：`on()` 泛型 lambda 参数从 `const Msg&` 改为 `Msg&`。现有 `handle(const Msg&)` 签名仍兼容（右值引用可绑 const 左值引用）
- **ServiceGateway**：`handle` 改为值传递 + `std::move`，转发 `AiChatServiceReq` 时 `messagesJson` 等大字段仅交换堆指针，不重新分配
- **其他 EO**：Router、SessionData、AiChatBus 等的 `handle(const Msg&)` 不受影响，`std::move` 在 const 路径无实际效果
- **性能**：透传场景的 `string`/`vector` 深拷贝消除，开销从 O(n) 降为 O(1)
- **编译期检查**：`const&` handler 中误用 `delegateTo` 会触发 `static_assert`，强制开发者显式选择值传递

---

## 备选方案

| 方案 | 否决原因 |
|------|----------|
| 不改 EoBase，只在 ServiceGateway 中手动注册值传递 handler | 每个需要零拷贝的 EO 都要绕开 `onMsg`，代码重复且易出错 |
| 将 `on()` 的 `Msg&` 改为 `Msg&&` | CAF 的 `message_handler` 不支持 `T&&` 参数，仅支持 `T&` 和 `const T&` |
| 所有 handler 强制值传递 | 纯消费 EO 不需要 move 语义，`const&` 更清晰地表达"只读不转发"的意图 |

---

## 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-21 | 初稿，采纳 |
