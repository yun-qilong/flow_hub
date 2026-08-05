#include "DPlane/session/SessionDispatcher.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;
using utils::LogLevel;

class TestSessionDispatcher : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        accessGateway_ = makeStub();
        router_ = makeStub();
        trackStub(accessGateway_);
        trackStub(router_);

        testee_ = spawn<DPlane::session::SessionDispatcher>(stubAddress(accessGateway_));

        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
        setRouterAddr();
    }

    void setRouterAddr()
    {
        sendToMeFrom(router_, testee_, TempConfig{3});
    }

    void restartWithoutRouter()
    {
        stopActor(testee_);
        testee_ = spawn<DPlane::session::SessionDispatcher>(stubAddress(accessGateway_));
        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
    }

    Stub accessGateway_;
    Stub router_;
};

TEST_F(TestSessionDispatcher, CheckHandleAiChatBusinessReq_DelegateToRouter)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "test";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(router_,
                                   [&](AiChatBusinessReq &msg) { EXPECT_EQ(msg.content, "test"); });
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatBusinessReq_RouterNotSet)
{
    restartWithoutRouter();

    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "should_drop";
    sendToMe(std::move(req));
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatBusinessResp_DelegateToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto resp = AiChatBusinessResp{};
    fillDefaultHead(resp);
    resp.content = "response";
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(accessGateway_,
                                    [&](AiChatBusinessResp &msg)
                                    {
                                        EXPECT_EQ(msg.head.sessionTaskId, kDefaultGtid);
                                        EXPECT_EQ(msg.content, "response");
                                    });
}

} // namespace
