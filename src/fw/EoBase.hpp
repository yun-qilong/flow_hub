// src/fw/EoBase.hpp
// EO 基类 — 对 CAF event_based_actor 的项目级封装。
//
// 业务 EO 继承此类，获得消息注册/收发/转发/延迟/请求-响应等能力，
// 无需接触 caf:: 命名空间。底层 actor 框架变更时，仅需修改本层文件：
//   - EoTypes.hpp   (类型别名)
//   - EoEnv.hpp     (actor_system 包装、孵化、匿名发送)
//   - EoBase.hpp    (actor 基类)
// 业务代码不受影响。

#pragma once

#include "fw/EoEnv.hpp"
#include "fw/EoTypes.hpp"
#include "utils/CrtpBase.hpp"

#include <iostream>
#include <utility>

// ===== Actor base class (hides caf::event_based_actor) =====

namespace fw
{

template <typename Derived>
class EoBase : public caf::event_based_actor, public utils::CrtpBase<Derived>
{
  public:
    explicit EoBase(EoConfig &cfg) : caf::event_based_actor(cfg) {}

    // 编译期标签：派生类覆盖为 true 表示 handler 中可能阻塞。
    // createEo 据此自动选择独立线程（detached）或共享池。
    static constexpr bool kMayBlock = false;

  protected:
    // ----- message handler registration -------------------------------
    // Call in init() for each message type this actor handles.
    //
    //   on<MyMsg>([this](const MyMsg& msg) {
    //       send(dest, msg);
    //   });
    template <typename Msg, typename F>
    void on(F &&handler)
    {
        auto mh =
            caf::message_handler{[this, h = std::forward<F>(handler)](const Msg &m) { h(m); }};
        if (mh_)
        {
            mh_ = mh_.or_else(std::move(mh));
        }
        else
        {
            mh_ = std::move(mh);
        }
    }

    // ----- default message handler ------------------------------------
    // Subclasses override this for specific message types.
    // If not overridden, prints a warning.
    template <typename Msg>
    void handle(const Msg & /*msg*/)
    {
        std::cerr << "[WARNING] Unhandled message type: " << typeid(Msg).name() << std::endl;
    }

    // ----- shorthand registration ------------------------------------
    // Registers this->handle(msg) as the handler for Msg.
    //
    //   init() {
    //       onMsg<MyMsg>();
    //       onMsg<OtherMsg>();
    //   }
    template <typename Msg>
    void onMsg()
    {
        on<Msg>([this](const Msg &msg) { this->getImplementation().handle(msg); });
    }

    // ----- message sending ------------------------------------------
    template <typename Msg>
    void sendTo(EoAddress target, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).send(target);
    }

    template <typename Msg>
    void delegateTo(EoAddress target, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).delegate(target);
    }

    template <typename Duration, typename Msg>
    void delaySendTo(EoAddress target, Duration d, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).schedule(d).send(target);
    }

    template <typename Msg>
    static void anonSendTo(EoAddress target, Msg &&msg)
    {
        fw::anonSendTo(target, std::forward<Msg>(msg));
    }

    // ----- request-response -----------------------------------------
    // 发送请求并注册异步回调（then 模式：等待响应期间可处理其他消息）
    // 仅用于 C 面直接通信，不经过 Router 的 D 面流转请使用 sendTo/delegateTo。
    template <typename Msg, typename OnValue, typename OnError>
    void requestThen(EoAddress target, EoDuration timeout, Msg &&msg, OnValue &&onValue,
                     OnError &&onError)
    {
        this->mail(std::forward<Msg>(msg))
            .request(target, timeout)
            .then(std::forward<OnValue>(onValue), std::forward<OnError>(onError));
    }

    // ----- self address ---------------------------------------------
    // 返回当前 EO 的地址引用，用于填入消息体供对端回消息时使用。
    EoAddress myAddress()
    {
        return caf::actor_cast<EoAddress>(this);
    }

    // 返回当前正在处理的消息的发送者地址。
    // 仅在 handler 执行期间有效（由 CAF 框架在调 handler 前自动设置）。
    EoAddress senderAddress()
    {
        return caf::actor_cast<EoAddress>(this->current_sender());
    }

    void stop()
    {
        caf::event_based_actor::quit();
    }

    // ----- subclass entry point --------------------------------------
    // Override to register message handlers via on<>().
    virtual void init() = 0;

  private:
    caf::behavior make_behavior() final
    {
        init();
        return caf::behavior{mh_};
    }

    caf::message_handler mh_;
};

} // namespace fw
