#pragma once

#include "fw/EoEnv.hpp"
#include "utils/SysLog.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>

// ===== Base fixture for EO unit tests =====
//
// Wraps all CAF primitives so test files contain zero CAF traces.
//
// Usage:
//   class TestMyEo : public fw::EoTestBase
//   {
//       void SetUp() override {
//           downstream_ = makeStub();
//           testee_ = spawn<MyEo>(stubHandle(downstream_));
//       }
//
//       Stub downstream_;
//       Actor testee_;
//   };

namespace fw
{

class EoTestBase : public ::testing::Test
{
  public:
    // ---- Opaque types (hide CAF internals) ----

    class Stub
    {
        friend class EoTestBase;
        std::unique_ptr<caf::scoped_actor> impl_;

      public:
        Stub() = default;
        explicit Stub(caf::actor_system &sys) : impl_(std::make_unique<caf::scoped_actor>(sys)) {}
    };

    using Actor = caf::actor;

  protected:
    // ---- System ----

    fw::EoEnv env_{};
    Stub stubEo_;
    Actor testee_{};
    std::vector<Stub *> trackedStubs_{&stubEo_};

    EoTestBase() : stubEo_(env_.system())
    {
        utils::gSysLog() = &mockSysLog_;
    }

    auto &system()
    {
        return env_.system();
    }

    void trackStub(Stub &s)
    {
        trackedStubs_.push_back(&s);
    }

    void verifyAllStubsEmpty(std::chrono::milliseconds timeout = std::chrono::milliseconds(10))
    {
        for (auto *s : trackedStubs_)
        {
            bool received = false;
            (*s->impl_)->receive([&](caf::message &) { received = true; },
                                 caf::after(timeout) >> [] {});
            EXPECT_FALSE(received) << "stub has unexpected remaining messages";
        }
    }

    void TearDown() override
    {
        stopActor(testee_);
        verifyAllStubsEmpty();
    }

    // ---- SysLog mock (shared across all EoTestBase fixtures) ----

    testing::StrictMock<utils::MockSysLog> mockSysLog_;

    // ---- Head field defaults (shared across all EO tests) ----

    static constexpr uint16_t kDefaultGtid = 0xAAAA;

    template <typename M>
    void fillDefaultHead(M &msg)
    {
        msg.head.sessionTaskId = kDefaultGtid;
        msg.head.busTaskIds.push_back(kDefaultGtid);
    }

    // ---- Stub / Actor factories ----

    Stub makeStub()
    {
        return Stub(system());
    }

    Actor stubAddress(const Stub &s)
    {
        return caf::actor_cast<Actor>(s.impl_->address());
    }

    template <typename T, typename... Args>
    Actor spawn(Args &&...args)
    {
        return system().spawn<T>(std::forward<Args>(args)...);
    }

    // ---- Messaging ----

    template <typename M>
    void sendToMe(M msg)
    {
        (*stubEo_.impl_)->mail(std::move(msg)).send(testee_);
    }

    template <typename M>
    void sendToMeFrom(Stub &sender, Actor target, M msg)
    {
        (*sender.impl_)->mail(std::move(msg)).send(target);
    }

    // ---- Verification ----

    template <typename M, typename F>
    void checkOutput(Stub &s, F check)
    {
        (*s.impl_)->receive([&](M &msg) { check(msg); });
    }

    template <typename M, typename F>
    void checkOutput(F check)
    {
        (*stubEo_.impl_)->receive([&](M &msg) { check(msg); });
    }

    template <typename Req, typename Resp, typename F>
    void checkOutputAndReply(Stub &s, F check, Resp response)
    {
        (*s.impl_)->receive(
            [&](Req &msg) -> Resp
            {
                check(msg);
                return response;
            });
    }

    // ---- Cleanup ----

    void stopActor(Actor &a)
    {
        caf::anon_send_exit(a, caf::exit_reason::user_shutdown);
    }
};

} // namespace fw
