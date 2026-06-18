# ADR-0017：编译期标签 `kMayBlock` 自动选择 Actor 线程模式

| 状态 | 日期 | 决策者 |
|------|------|--------|
| 已采纳 | 2026-06-18 | 韵启龙 |

---

## 背景

EO 分为两类：

| 类型 | 示例 | handler 行为 |
|------|------|-------------|
| 非阻塞 EO | Router、AiChatBus、SessionMgr | 纯计算/消息转发，从不阻塞 |
| 阻塞 EO | AiApiAdapter | 需 `curl_easy_perform()` 同步等待 HTTP 响应 |

CAF 对两类 EO 的实例化方式不同：

- 非阻塞 EO：`sys.spawn<T>(...)` — 运行于共享 Actor 调度池
- 阻塞 EO：`sys.spawn<T, caf::detached>(...)` — 独占 OS 线程，阻塞不影响共享池

如果程序员选错了实例化方式（对阻塞 EO 用了共享池），会导致该线程上的其他 Actor 被一起卡死且编译器毫无提示。这种错误是运行时静默发生的，难以排查。

此前 `EoEnv` 提供两个并行的创建接口 `createEo` 和 `createDetachedEo`，选择权完全落在程序员身上，增加了出错的可能性。

## 决策

### 1. 在 `EoBase` 中增加编译期标签 `kMayBlock`

```cpp
template <typename Derived>
class EoBase : ... {
  public:
    static constexpr bool kMayBlock = false;  // 默认非阻塞
};
```

阻塞 EO 在其类声明中覆盖：

```cpp
class AiApiAdapter : public fw::EoBase<AiApiAdapter> {
  public:
    static constexpr bool kMayBlock = true;  // handler 中有阻塞 HTTP 调用
};
```

### 2. `createEo` 通过 `if constexpr` 自动选择

```cpp
template <typename T, typename... Args>
EoAddress createEo(Args &&...args) {
    if constexpr (T::kMayBlock) {
        return sys_.spawn<T, caf::detached>(...);   // 独立线程
    } else {
        return sys_.spawn<T>(...);                   // 共享池
    }
}
```

程序员只需一种调用方式：`env.createEo<T>(...)`。框架根据 `T::kMayBlock` 在编译期自动决策，零运行时开销。

### 3. 移除 `createDetachedEo` 和 `createStandaloneEo`

这两个接口在 `kMayBlock` 机制下成为冗余，移除后`EoEnv`只剩 `createEo` 一个创建入口。

## 为什么不是其他方案

| 方案 | 否决原因 |
|------|----------|
| 程序员自行判断用 `createEo` 还是 `createDetachedEo` | 运行时才会暴露错误，难以排查；增加心智负担 |
| 运行时检测（如检查 handler 中是否有阻塞调用） | C++ 无法在运行时可靠地判断一段代码是否会阻塞 |
| 一律使用 detached | 每个 Actor 一个线程，资源浪费严重（线程栈、上下文切换开销） |
| 使用 `[[attribute]]` 或自定义 annotation | 无标准编译器支持，Clang/GCC 无此类内置属性 |

## 影响

- `fw/EoBase.hpp`：新增 `static constexpr bool kMayBlock = false`
- `fw/EoEnv.hpp`：`createEo` 增加 `if constexpr` 分支；移除 `createDetachedEo`、`createStandaloneEo`
- `DPlane/service/AiApiAdapter.hpp`：覆盖 `kMayBlock = true`
- 所有现有 EO（Router、AiChatBus、SessionMgr、ServiceGateway、SessionData、CliAdapter）无需修改，默认为 `false`（非阻塞）
- 未来新增阻塞 EO 时，只需在类声明中加一行 `static constexpr bool kMayBlock = true`，无需改动 `main.cpp` 中的 `createEo` 调用
