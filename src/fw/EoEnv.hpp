// src/fw/EoEnv.hpp
// EO 运行时环境 — 包装 actor_system 的创建、EO 孵化、匿名发送。
//
// 业务代码只需 include 此文件即可使用全部 EO 基础设施，
// 无需直接接触 caf:: 命名空间。

#pragma once

#include "fw/EoTypes.hpp"
#include "generated/message/Messages.hpp"

#include <utility>

namespace fw
{

// ===== 匿名发送（等价 caf::anon_mail(msg).send(target)） =====
// 自由函数，不依赖 EoEnv 实例，任何上下文均可调用。

template <typename Msg>
void anonSendTo(EoAddress target, Msg &&msg)
{
    caf::anon_mail(std::forward<Msg>(msg)).send(target);
}

// ===== EO 运行时环境 =====
// 包装 caf::actor_system，提供 EO 孵化、分离 actor、全局等待。
// 构造时自动完成 CAF 全局元对象初始化。

class EoEnv
{
  public:
    // 使用默认配置构造，自动初始化 CAF 元对象
    EoEnv() : cfg_{}, sys_{cfg_} {}

    // 使用自定义配置构造
    explicit EoEnv(EoSystemConfig c) : cfg_{std::move(c)}, sys_{cfg_} {}

    EoEnv(const EoEnv &) = delete;
    EoEnv &operator=(const EoEnv &) = delete;
    EoEnv(EoEnv &&) = delete;
    EoEnv &operator=(EoEnv &&) = delete;

    // ----- 创建 EO -------------------------------------------
    //   阻塞 I/O 类（kMayBlock=true）自动以独立线程创建。
    //   auto mgr = env.createEo<CPlane::BusinessMgr>();
    template <typename T, typename... Args>
    EoAddress createEo(Args &&...args)
    {
        if constexpr (T::kMayBlock)
        {
            return sys_.spawn<T, caf::detached>(std::forward<Args>(args)...);
        }
        else
        {
            return sys_.spawn<T>(std::forward<Args>(args)...);
        }
    }

    // ----- 等待全部普通 EO 结束 -----------------------------------
    void awaitAllDone()
    {
        sys_.await_all_actors_done();
    }

  private:
    // 利用成员声明顺序确保 CAF 全局初始化在 actor_system 构造之前完成
    struct MetaInit
    {
        MetaInit()
        {
            caf::init_global_meta_objects<caf::id_block::flowhub>();
            caf::core::init_global_meta_objects();
        }
    };

    MetaInit metaInit_; // 必须最先声明 — 先于 cfg_ 和 sys_
    EoSystemConfig cfg_;
    caf::actor_system sys_;
};

} // namespace fw
