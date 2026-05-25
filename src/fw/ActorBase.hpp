// src/fw/ActorBase.hpp
// Framework-agnostic actor abstraction.
//
// Hides CAF-specific types and APIs behind project-neutral names.
// If the underlying actor framework changes, only this file and
// Macros.hpp need updating — business actors are unaffected.

#pragma once

#include "fw/ActorTypes.hpp"
#include "generated/message/Messages.hpp"
#include "utils/CrtpBase.hpp"

#include <iostream>
#include <utility>

// ===== Actor base class (hides caf::event_based_actor) =====

namespace fw
{

template <typename Derived>
class ActorBase : public caf::event_based_actor, public utils::CrtpBase<Derived>
{
  public:
    explicit ActorBase(ActorConfig &cfg) : caf::event_based_actor(cfg) {}

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
    void sendTo(ActorRef target, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).send(target);
    }

    template <typename Msg>
    void delegateTo(ActorRef target, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).delegate(target);
    }

    template <typename Duration, typename Msg>
    void delaySendTo(ActorRef target, Duration d, Msg &&msg)
    {
        this->mail(std::forward<Msg>(msg)).schedule(d).send(target);
    }

    template <typename Msg>
    static void anonSendTo(ActorRef target, Msg &&msg)
    {
        caf::anon_mail(std::forward<Msg>(msg)).send(target);
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
