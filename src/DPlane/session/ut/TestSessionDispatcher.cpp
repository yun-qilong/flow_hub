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
        aiAgora_ = makeStub();
        trackStub(accessGateway_);
        trackStub(router_);
        trackStub(aiAgora_);

        testee_ = spawn<DPlane::session::SessionDispatcher>(stubAddress(accessGateway_));

        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
        setRouterAddr();
        setAiAgoraAddr();
    }

    void setRouterAddr()
    {
        sendToMeFrom(router_, testee_, TempConfig{3});
    }

    void setAiAgoraAddr()
    {
        sendToMeFrom(aiAgora_, testee_, TempConfig{7});
    }

    void restartWithoutRouter()
    {
        stopActor(testee_);
        testee_ = spawn<DPlane::session::SessionDispatcher>(stubAddress(accessGateway_));
        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
    }

    void restartWithoutAiAgora()
    {
        stopActor(testee_);
        testee_ = spawn<DPlane::session::SessionDispatcher>(stubAddress(accessGateway_));
        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
        setRouterAddr();
    }

    Stub accessGateway_;
    Stub router_;
    Stub aiAgora_;
};

TEST_F(TestSessionDispatcher, CheckHandleAiChatResp_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto resp = AiChatResp{};
    resp.head.sessionTaskId = 0x7001;
    resp.head.busTaskIds = {0x7001};
    resp.content = "response";
    sendToMe(std::move(resp));

    checkOutput<AiChatResp>(aiAgora_,
                            [&](AiChatResp &msg)
                            {
                                EXPECT_EQ(msg.head.sessionTaskId, 0x7001);
                                EXPECT_EQ(msg.content, "response");
                            });
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatResp_AiAgoraNotSet)
{
    restartWithoutAiAgora();

    EXPECT_LOG(LogLevel::ERR, 1);

    auto resp = AiChatResp{};
    resp.head.sessionTaskId = 0x7001;
    resp.head.busTaskIds = {0x7001};
    resp.content = "should_drop";
    sendToMe(std::move(resp));
}

TEST_F(TestSessionDispatcher, CheckHandleAiChatConfigResp_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto resp = AiChatConfigResp{};
    resp.head.sessionTaskId = 0x7001;
    resp.head.busTaskIds = {0x7001};
    resp.isSuccess = true;
    sendToMe(std::move(resp));

    checkOutput<AiChatConfigResp>(aiAgora_, [&](AiChatConfigResp &msg)
                                  { EXPECT_EQ(msg.head.sessionTaskId, 0x7001); });
}

TEST_F(TestSessionDispatcher, CheckHandleTaskConfigReq_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = TaskConfigReq{};
    req.head.sessionTaskId = 0x7001;
    req.head.busTaskIds = {0x7001};
    req.payload = "{}";
    sendToMe(std::move(req));

    checkOutput<TaskConfigReq>(aiAgora_, [&](TaskConfigReq &msg)
                               { EXPECT_EQ(msg.head.sessionTaskId, 0x7001); });
}

TEST_F(TestSessionDispatcher, CheckHandleAiAgoraChatReq_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiAgoraChatReq{};
    req.head.sessionTaskId = 0x7001;
    req.head.busTaskIds = {0x7001};
    req.content = "hi";
    sendToMe(std::move(req));

    checkOutput<AiAgoraChatReq>(aiAgora_, [&](AiAgoraChatReq &msg)
                                { EXPECT_EQ(msg.head.sessionTaskId, 0x7001); });
}

TEST_F(TestSessionDispatcher, CheckHandleAiAgoraResetReq_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiAgoraResetReq{};
    req.head.sessionTaskId = 0x7001;
    req.head.busTaskIds = {0x7001};
    sendToMe(std::move(req));

    checkOutput<AiAgoraResetReq>(aiAgora_, [&](AiAgoraResetReq &msg)
                                 { EXPECT_EQ(msg.head.sessionTaskId, 0x7001); });
}

TEST_F(TestSessionDispatcher, CheckHandleTaskDeleteReq_DelegateToAiAgora)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = TaskDeleteReq{};
    req.head.sessionTaskId = 0x7001;
    req.head.busTaskIds = {0x7001};
    sendToMe(std::move(req));

    checkOutput<TaskDeleteReq>(aiAgora_, [&](TaskDeleteReq &msg)
                               { EXPECT_EQ(msg.head.sessionTaskId, 0x7001); });
}

TEST_F(TestSessionDispatcher, CheckHandleInvalidTaskType_Drop)
{
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = TaskConfigReq{};
    req.head.sessionTaskId = 0x9001;
    req.head.busTaskIds = {0x9001};
    req.payload = "{}";
    sendToMe(std::move(req));
}

} // namespace
