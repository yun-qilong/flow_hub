#include "DPlane/session/SessionData.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;
using utils::LogLevel;

class TestSessionData : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        accessGateway_ = makeStub();
        router_ = makeStub();
        trackStub(accessGateway_);
        trackStub(router_);

        testee_ = spawn<DPlane::session::SessionData>(stubAddress(accessGateway_));

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
        testee_ = spawn<DPlane::session::SessionData>(stubAddress(accessGateway_));
        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
    }

    Stub accessGateway_;
    Stub router_;
};

TEST_F(TestSessionData, CheckHandleAiChatBusinessReq_ForwardToRouter)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "test";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(router_,
                                   [&](AiChatBusinessReq &msg) { EXPECT_EQ(msg.content, "test"); });
}

TEST_F(TestSessionData, CheckHandleAiChatBusinessReq_RouterNotSet)
{
    restartWithoutRouter();

    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "should_drop";
    sendToMe(std::move(req));
}

TEST_F(TestSessionData, CheckHandleAiChatBusinessResp_ForwardToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto resp = AiChatBusinessResp{};
    fillDefaultHead(resp);
    resp.content = "response";
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(accessGateway_,
                                    [&](AiChatBusinessResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        EXPECT_EQ(msg.head.gtidList.at(0), kDefaultGtid);
                                        EXPECT_EQ(msg.content, "response");
                                    });
}

} // namespace
