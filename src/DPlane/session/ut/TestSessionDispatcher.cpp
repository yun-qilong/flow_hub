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

TEST_F(TestSessionDispatcher, CheckHandleAiChatReq_DelegateToRouter)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiChatReq{};
    fillDefaultHead(req);
    req.messagesJson = "test";
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(router_, [&](AiChatReq &msg) { EXPECT_EQ(msg.messagesJson, "test"); });
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatReq_RouterNotSet)
{
    restartWithoutRouter();

    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = AiChatReq{};
    fillDefaultHead(req);
    req.messagesJson = "should_drop";
    sendToMe(std::move(req));
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatResp_DelegateToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto resp = AiChatResp{};
    fillDefaultHead(resp);
    resp.content = "response";
    sendToMe(std::move(resp));

    checkOutput<AiChatResp>(accessGateway_,
                            [&](AiChatResp &msg)
                            {
                                EXPECT_EQ(msg.head.sessionTaskId, kDefaultGtid);
                                EXPECT_EQ(msg.content, "response");
                            });
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatResp_GatewayNotSet)
{
    stopActor(testee_);
    testee_ = spawn<DPlane::session::SessionDispatcher>(fw::EoAddress{});

    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    auto resp = AiChatResp{};
    fillDefaultHead(resp);
    resp.content = "should_drop";
    sendToMe(std::move(resp));
}

} // namespace
