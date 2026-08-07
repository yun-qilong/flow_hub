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

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_InvalidState)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    EXPECT_LOG(LogLevel::WRN, 1);
    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutput<AiAgoraChatResp>(accessGateway_,
                                 [&](AiAgoraChatResp &resp)
                                 {
                                     EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                     EXPECT_TRUE(resp.isComplete);
                                     EXPECT_FALSE(resp.hasResponses);
                                     EXPECT_EQ(resp.errorCode, 5);
                                 });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_Success)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_,
        [&](AiChatReq &req)
        {
            EXPECT_EQ(req.head.sessionTaskId, sessionTaskId);
            ASSERT_EQ(req.head.busTaskIds.size(), 1u);
            EXPECT_EQ(req.head.busTaskIds.at(0), kBusTaskId);
            EXPECT_NE(req.messagesJson.find("hello"), std::string::npos);
            EXPECT_NE(req.messagesJson.find("\"role\":\"user\""), std::string::npos);
        },
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, true, 0, "hi back"};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_,
                                 [&](AiAgoraChatResp &resp)
                                 {
                                     EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                     EXPECT_TRUE(resp.isComplete);
                                     EXPECT_TRUE(resp.hasResponses);
                                     EXPECT_EQ(resp.endReason, 1);
                                     EXPECT_EQ(resp.errorCode, 0);
                                     EXPECT_EQ(resp.responses, "hi back");
                                 });

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed([](common::context::AiAgoraContext &ctx) { EXPECT_EQ(ctx.state, 2); },
                     []() { FAIL() << "context missing"; });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_SuccessFalse)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_, [&](AiChatReq &) {},
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, false, 0, ""};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_,
                                 [&](AiAgoraChatResp &resp)
                                 {
                                     EXPECT_TRUE(resp.isComplete);
                                     EXPECT_FALSE(resp.hasResponses);
                                     EXPECT_EQ(resp.errorCode, 1);
                                 });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_DuplicateRespIgnored)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_, [&](AiChatReq &) {},
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, true, 0, "first"};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_, [&](AiAgoraChatResp &resp)
                                 { EXPECT_EQ(resp.responses, "first"); });

    auto dupHead = UserHead{};
    dupHead.sessionTaskId = sessionTaskId;
    dupHead.busTaskIds = {kBusTaskId};
    sendToMeFrom(router_, testee_, AiChatResp{dupHead, true, 0, "duplicate"});

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [](common::context::AiAgoraContext &ctx)
            {
                EXPECT_EQ(ctx.state, 2);
                EXPECT_EQ(ctx.pendingReplies, 0);
            },
            []() { FAIL() << "context missing"; });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_ContextFullAtStart)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [](common::context::AiAgoraContext &ctx)
            { ctx.topicBaseJsonSize = static_cast<uint32_t>(ctx.topicBaseJson.size() - 100); },
            []() { FAIL() << "context missing"; });

    EXPECT_LOG(LogLevel::WRN, 1);
    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutput<AiAgoraChatResp>(accessGateway_,
                                 [&](AiAgoraChatResp &resp)
                                 {
                                     EXPECT_TRUE(resp.isComplete);
                                     EXPECT_EQ(resp.errorCode, 2);
                                 });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_ContextFullMidRound)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [](common::context::AiAgoraContext &ctx)
            { ctx.topicBaseJsonSize = static_cast<uint32_t>(ctx.topicBaseJson.size() - 3000); },
            []() { FAIL() << "context missing"; });

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_, [&](AiChatReq &) {},
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, true, 0, std::string(5000, 'x')};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_,
                                 [&](AiAgoraChatResp &resp)
                                 {
                                     EXPECT_TRUE(resp.isComplete);
                                     EXPECT_FALSE(resp.hasResponses);
                                     EXPECT_EQ(resp.errorCode, 3);
                                 });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_AppendAndEscape)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    std::string content = "say \"hi\" \\ done";
    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, content});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_,
        [&](AiChatReq &req)
        { EXPECT_NE(req.messagesJson.find("say \\\"hi\\\""), std::string::npos); },
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, true, 0, "reply one"};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_, [&](AiAgoraChatResp &) {});

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "second"});
    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_,
        [&](AiChatReq &req)
        {
            EXPECT_NE(req.messagesJson.find("\"content\":\"reply one\""), std::string::npos);
            EXPECT_NE(req.messagesJson.find("second"), std::string::npos);
        },
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            head.busTaskIds = {kBusTaskId};
            return AiChatResp{head, true, 0, "reply two"};
        }());

    checkOutput<AiAgoraChatResp>(accessGateway_, [&](AiAgoraChatResp &) {});
}

} // namespace
