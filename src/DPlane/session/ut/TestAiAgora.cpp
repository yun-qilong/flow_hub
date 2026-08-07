#include "common/TaskPool.hpp"
#include "DPlane/session/AiAgora.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

using namespace common::message;
using common::TaskType;
using utils::LogLevel;

class TestAiAgora : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        sessionDispatcher_ = makeStub();
        sessionMgr_ = makeStub();
        router_ = makeStub();
        accessGateway_ = makeStub();
        trackStub(sessionDispatcher_);
        trackStub(sessionMgr_);
        trackStub(router_);
        trackStub(accessGateway_);

        testee_ = spawn<DPlane::session::AiAgora>(stubAddress(sessionDispatcher_),
                                                  stubAddress(sessionMgr_), stubAddress(router_),
                                                  stubAddress(accessGateway_), pool_);

        checkOutput<TempConfig>(sessionDispatcher_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 7); });
    }

    common::GTID allocateSession()
    {
        common::GTID sessionTaskId = common::kInvalidGtid;
        pool_.allocate(TaskType::AiAgora)
            .useOrFailed([&](common::GTID &gtid) { sessionTaskId = gtid; }, []() {});
        return sessionTaskId;
    }

    static std::string validPayload()
    {
        return R"({"aiCount":1,"hasJudge":false,"maxRounds":5,"maxResponseLength":500,)"
               R"("timeoutMs":30000,"configs":[{"apiUrl":"u","apiKey":"k","model":"m",)"
               R"("systemPrompt":"sys","temperature":0.7}]})";
    }

    void replyBusTaskCreate(common::GTID sessionTaskId, bool isSuccess)
    {
        auto head = UserHead{};
        head.sessionTaskId = sessionTaskId;
        if (isSuccess)
        {
            head.busTaskIds = {kBusTaskId};
        }
        checkOutputAndReply<BusTaskCreateReq, BusTaskCreateResp>(
            sessionMgr_, [](BusTaskCreateReq &) {}, BusTaskCreateResp{head, isSuccess});
    }

    void replyAiChatConfig(common::GTID sessionTaskId, uint8_t aiIndex, bool isSuccess)
    {
        auto head = UserHead{};
        head.sessionTaskId = sessionTaskId;
        head.busTaskIds = {kBusTaskId};
        sendToMeFrom(router_, testee_, AiChatConfigResp{head, isSuccess, aiIndex});
    }

    void runSuccessFlow(common::GTID sessionTaskId)
    {
        sendToMe(TaskConfigReq{UserHead{sessionTaskId, {}}, validPayload()});

        checkOutputAndReply<BusTaskCreateReq, BusTaskCreateResp>(
            sessionMgr_,
            [&](BusTaskCreateReq &req)
            {
                EXPECT_EQ(req.head.sessionTaskId, sessionTaskId);
                ASSERT_EQ(req.taskTypes.size(), 1u);
                EXPECT_EQ(req.taskTypes.at(0), TaskType::AiChat);
            },
            [&]()
            {
                auto head = UserHead{};
                head.sessionTaskId = sessionTaskId;
                head.busTaskIds = {kBusTaskId};
                return BusTaskCreateResp{head, true};
            }());

        checkOutput<AiChatConfigReq>(router_,
                                     [&](AiChatConfigReq &req)
                                     {
                                         EXPECT_EQ(req.head.sessionTaskId, sessionTaskId);
                                         ASSERT_EQ(req.head.busTaskIds.size(), 1u);
                                         EXPECT_EQ(req.head.busTaskIds.at(0), kBusTaskId);
                                         EXPECT_EQ(req.aiIndex, 0);
                                         EXPECT_NE(req.systemPrompt.find("sys"), std::string::npos);
                                         EXPECT_NE(req.payload.find("\"apiUrl\":\"u\""),
                                                   std::string::npos);
                                     });

        replyAiChatConfig(sessionTaskId, 0, true);

        checkOutput<TaskConfigResp>(accessGateway_,
                                    [&](TaskConfigResp &resp)
                                    {
                                        EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                        EXPECT_TRUE(resp.isSuccess);
                                        EXPECT_GT(resp.estimatedTopicCount, 0u);
                                    });
    }

    common::TaskPool pool_{};
    Stub sessionDispatcher_;
    Stub sessionMgr_;
    Stub router_;
    Stub accessGateway_;
    static constexpr common::GTID kBusTaskId = 0x9001;
};

TEST_F(TestAiAgora, CheckConstructor_RegisterTempConfig7)
{
    sendToMe(TempConfig{0});
}

TEST_F(TestAiAgora, CheckHandleTaskConfigReq_Success)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    runSuccessFlow(sessionTaskId);

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed([](common::context::AiAgoraContext &ctx) { EXPECT_EQ(ctx.state, 2); },
                     []() { FAIL() << "context missing"; });
}

TEST_F(TestAiAgora, CheckHandleTaskConfigReq_InvalidPayload)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    EXPECT_LOG(LogLevel::WRN, 1);

    sendToMe(TaskConfigReq{UserHead{sessionTaskId, {}}, "{not-json"});

    checkOutput<TaskConfigResp>(accessGateway_,
                                [&](TaskConfigResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                    EXPECT_FALSE(resp.isSuccess);
                                });
}

TEST_F(TestAiAgora, CheckHandleTaskConfigReq_NotUnconfigured)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    runSuccessFlow(sessionTaskId);

    EXPECT_LOG(LogLevel::WRN, 1);
    sendToMe(TaskConfigReq{UserHead{sessionTaskId, {}}, validPayload()});

    checkOutput<TaskConfigResp>(accessGateway_,
                                [&](TaskConfigResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                    EXPECT_FALSE(resp.isSuccess);
                                });
}

TEST_F(TestAiAgora, CheckHandleTaskConfigReq_NoSessionContext)
{
    EXPECT_LOG(LogLevel::WRN, 1);

    sendToMe(TaskConfigReq{UserHead{0x7000, {}}, validPayload()});

    checkOutput<TaskConfigResp>(accessGateway_,
                                [&](TaskConfigResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, 0x7000);
                                    EXPECT_FALSE(resp.isSuccess);
                                });
}

TEST_F(TestAiAgora, CheckHandleBusTaskCreateResp_Failure)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    sendToMe(TaskConfigReq{UserHead{sessionTaskId, {}}, validPayload()});

    EXPECT_LOG(LogLevel::WRN, 1);
    replyBusTaskCreate(sessionTaskId, false);

    checkOutput<TaskConfigResp>(accessGateway_,
                                [&](TaskConfigResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                    EXPECT_FALSE(resp.isSuccess);
                                });
}

TEST_F(TestAiAgora, CheckHandleAiChatConfigResp_Failure)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    sendToMe(TaskConfigReq{UserHead{sessionTaskId, {}}, validPayload()});
    replyBusTaskCreate(sessionTaskId, true);
    checkOutput<AiChatConfigReq>(router_, [](AiChatConfigReq &) {});

    EXPECT_LOG(LogLevel::WRN, 1);
    replyAiChatConfig(sessionTaskId, 0, false);

    checkOutput<BusTaskDeleteReq>(sessionMgr_,
                                  [&](BusTaskDeleteReq &req)
                                  {
                                      EXPECT_EQ(req.head.sessionTaskId, sessionTaskId);
                                      ASSERT_EQ(req.head.busTaskIds.size(), 9U);
                                      EXPECT_EQ(req.head.busTaskIds.at(0), kBusTaskId);
                                      for (size_t i = 1; i < req.head.busTaskIds.size(); ++i)
                                      {
                                          EXPECT_EQ(req.head.busTaskIds.at(i),
                                                    common::kInvalidGtid);
                                      }
                                  });

    checkOutput<TaskConfigResp>(accessGateway_,
                                [&](TaskConfigResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                    EXPECT_FALSE(resp.isSuccess);
                                });
}

} // namespace
