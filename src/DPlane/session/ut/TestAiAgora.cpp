#include "common/TaskPool.hpp"
#include "DPlane/session/AiAgora.hpp"
#include "fw/EoTestBase.hpp"

#include <nlohmann/json.hpp>

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

    checkOutputAndReply<BusTaskDeleteReq, BusTaskDeleteResp>(
        sessionMgr_,
        [&](BusTaskDeleteReq &req)
        {
            EXPECT_EQ(req.head.sessionTaskId, sessionTaskId);
            ASSERT_EQ(req.head.busTaskIds.size(), 9U);
            EXPECT_EQ(req.head.busTaskIds.at(0), kBusTaskId);
            for (size_t i = 1; i < req.head.busTaskIds.size(); ++i)
            {
                EXPECT_EQ(req.head.busTaskIds.at(i), common::kInvalidGtid);
            }
        },
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            return BusTaskDeleteResp{head, true};
        }());

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
            auto parsed = nlohmann::json::parse(req.messagesJson);
            ASSERT_TRUE(parsed.is_array());
            ASSERT_EQ(parsed.size(), 1u);
            EXPECT_EQ(parsed.at(0).at("role").get<std::string>(), "user");
            EXPECT_EQ(parsed.at(0).at("content").get<std::string>(), "hello");
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

TEST_F(TestAiAgora, CheckHandleAiAgoraChatReq_SpecialChars_ValidJson)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello \"quoted\" 你好\nline"});

    checkOutputAndReply<AiChatReq, AiChatResp>(
        router_,
        [&](AiChatReq &req)
        {
            auto parsed = nlohmann::json::parse(req.messagesJson);
            ASSERT_TRUE(parsed.is_array());
            ASSERT_EQ(parsed.size(), 1u);
            EXPECT_EQ(parsed.at(0).at("role").get<std::string>(), "user");
            EXPECT_EQ(parsed.at(0).at("content").get<std::string>(), "hello \"quoted\" 你好\nline");
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
                                     EXPECT_EQ(resp.errorCode, 0);
                                 });
}

TEST_F(TestAiAgora, CheckCompleteConfig_InitializesTopicBase)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [](common::context::AiAgoraContext &ctx)
            {
                EXPECT_EQ(ctx.topicBaseJsonSize, 2U);
                EXPECT_EQ(ctx.topicBaseJson.at(0), static_cast<uint8_t>('['));
                EXPECT_EQ(ctx.topicBaseJson.at(1), static_cast<uint8_t>(']'));
                EXPECT_EQ(ctx.state, 2);
            },
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

TEST_F(TestAiAgora, CheckHandleAiAgoraResetReq_NotConfigured)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    sendToMe(AiAgoraResetReq{UserHead{sessionTaskId, {}}});

    checkOutput<AiAgoraResetResp>(accessGateway_,
                                  [&](AiAgoraResetResp &resp)
                                  {
                                      EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                      EXPECT_FALSE(resp.isSuccess);
                                  });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraResetReq_Configuring)
{
    constexpr uint8_t kConfiguringState = 1;

    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed([](common::context::AiAgoraContext &ctx) { ctx.state = kConfiguringState; },
                     []() { FAIL() << "context missing"; });

    sendToMe(AiAgoraResetReq{UserHead{sessionTaskId, {}}});

    checkOutput<AiAgoraResetResp>(accessGateway_,
                                  [&](AiAgoraResetResp &resp)
                                  {
                                      EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                      EXPECT_FALSE(resp.isSuccess);
                                  });
}

TEST_F(TestAiAgora, CheckHandleTaskDeleteReq_Cascade)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(TaskDeleteReq{UserHead{sessionTaskId, {}}});

    checkOutputAndReply<BusTaskDeleteReq, BusTaskDeleteResp>(
        sessionMgr_,
        [&](BusTaskDeleteReq &req)
        {
            ASSERT_EQ(req.head.busTaskIds.size(), 9u);
            EXPECT_EQ(req.head.busTaskIds.at(0), kBusTaskId);
        },
        [&]()
        {
            auto head = UserHead{};
            head.sessionTaskId = sessionTaskId;
            return BusTaskDeleteResp{head, true};
        }());
    checkOutput<TaskDeleteReq>(sessionMgr_, [&](TaskDeleteReq &req)
                               { EXPECT_EQ(req.head.sessionTaskId, sessionTaskId); });
    checkOutput<TaskDeleteResp>(accessGateway_,
                                [&](TaskDeleteResp &resp)
                                {
                                    EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                    EXPECT_TRUE(resp.isSuccess);
                                });
}

TEST_F(TestAiAgora, CheckHandleTaskDeleteReq_NoContext)
{
    constexpr common::GTID kUnallocated = 0x7001;

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_)).Times(1);

    sendToMe(TaskDeleteReq{UserHead{kUnallocated, {}}});

    checkOutput<TaskDeleteReq>(sessionMgr_, [&](TaskDeleteReq &req)
                               { EXPECT_EQ(req.head.sessionTaskId, kUnallocated); });
    checkOutput<TaskDeleteResp>(accessGateway_,
                                [](TaskDeleteResp &resp) { EXPECT_TRUE(resp.isSuccess); });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraResetReq_Success)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraResetReq{UserHead{sessionTaskId, {}}});

    checkOutput<AiAgoraResetResp>(accessGateway_,
                                  [&](AiAgoraResetResp &resp)
                                  {
                                      EXPECT_EQ(resp.head.sessionTaskId, sessionTaskId);
                                      EXPECT_TRUE(resp.isSuccess);
                                      EXPECT_GT(resp.estimatedTopicCount, 0U);
                                  });

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [&](common::context::AiAgoraContext &ctx)
            {
                EXPECT_EQ(ctx.state, 2);
                EXPECT_EQ(ctx.topicBaseJsonSize, 2U);
                EXPECT_EQ(ctx.currentRound, 0U);
                EXPECT_EQ(ctx.pendingReplies, 0U);
                EXPECT_EQ(ctx.debateTaskIds.at(0), kBusTaskId);
            },
            []() { FAIL() << "context missing"; });
}

TEST_F(TestAiAgora, CheckHandleAiAgoraResetReq_MidDebate)
{
    auto sessionTaskId = allocateSession();
    ASSERT_NE(sessionTaskId, common::kInvalidGtid);
    runSuccessFlow(sessionTaskId);

    sendToMe(AiAgoraChatReq{UserHead{sessionTaskId, {}}, "hello"});
    checkOutput<AiChatReq>(router_, [](AiChatReq &) {});

    sendToMe(AiAgoraResetReq{UserHead{sessionTaskId, {}}});

    checkOutput<AiAgoraResetResp>(accessGateway_,
                                  [&](AiAgoraResetResp &resp) { EXPECT_TRUE(resp.isSuccess); });

    auto lateHead = UserHead{};
    lateHead.sessionTaskId = sessionTaskId;
    lateHead.busTaskIds = {kBusTaskId};
    sendToMeFrom(router_, testee_, AiChatResp{lateHead, true, 0, "late"});

    pool_.getContext<common::context::AiAgoraContext>(sessionTaskId)
        .useOrFailed(
            [](common::context::AiAgoraContext &ctx)
            {
                EXPECT_EQ(ctx.state, 2);
                EXPECT_EQ(ctx.pendingReplies, 0);
                EXPECT_EQ(ctx.topicBaseJsonSize, 2U);
            },
            []() { FAIL() << "context missing"; });
}

} // namespace
