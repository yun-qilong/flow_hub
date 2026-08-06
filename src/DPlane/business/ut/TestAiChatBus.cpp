#include "common/TaskPool.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

using namespace common::message;
using AiChatBus = DPlane::business::AiChatBus<common::TaskType::AiChat>;
using AiChatContext = common::context::AiChatContext;

constexpr std::string_view kValidPayload =
    R"({"apiUrl":"https://api.example.com","apiKey":"sk-key","model":"m1","temperature":0.7})";

constexpr uint8_t kInvalidAiIndex = 0xFF;

class TestAiChatBus : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        sessionDispatcherStub_ = makeStub();
        businessMgrStub_ = makeStub();
        routerStub_ = makeStub();
        serviceGatewayStub_ = makeStub();

        trackStub(sessionDispatcherStub_);
        trackStub(businessMgrStub_);
        trackStub(routerStub_);
        trackStub(serviceGatewayStub_);

        pool_.allocate(common::TaskType::AiChat)
            .useOrFailed([&](common::GTID g) { gtid_ = g; },
                         [] { FAIL() << "failed to allocate GTID"; });

        testee_ = spawn<AiChatBus>(pool_, stubAddress(sessionDispatcherStub_),
                                   stubAddress(businessMgrStub_), stubAddress(routerStub_));

        checkOutput<TempConfig>(routerStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 6); });
    }

    void registerServiceGateway()
    {
        sendToMeFrom(serviceGatewayStub_, testee_, TempConfig{9});
    }

    template <typename F>
    void withCtx(F &&fn)
    {
        pool_.getContext<AiChatContext>(gtid_).useOrFailed([&](AiChatContext &ctx) { fn(ctx); },
                                                           [] { FAIL() << "context not found"; });
    }

    static std::string readCString(const std::array<uint8_t, 64> &data)
    {
        std::string out;
        for (size_t i = 0; i < data.size() and data.at(i) != 0; ++i)
        {
            out.push_back(static_cast<char>(data.at(i)));
        }
        return out;
    }

    AiChatConfigReq makeConfigReq(std::string payload)
    {
        AiChatConfigReq req;
        req.head.sessionTaskId = gtid_;
        req.head.busTaskIds = {gtid_};
        req.aiIndex = 0;
        req.systemPrompt = "system prompt";
        req.payload = std::move(payload);
        return req;
    }

    AiChatReq makeChatReq()
    {
        AiChatReq req;
        req.head.sessionTaskId = gtid_;
        req.head.busTaskIds = {gtid_};
        req.messagesJson = R"([{"role":"user","content":"hi"}])";
        return req;
    }

    common::TaskPool pool_;
    common::GTID gtid_ = 0;

    Stub sessionDispatcherStub_;
    Stub businessMgrStub_;
    Stub routerStub_;
    Stub serviceGatewayStub_;
};

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_Success)
{
    registerServiceGateway();

    sendToMe(makeConfigReq(std::string(kValidPayload)));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_TRUE(msg.isSuccess); });

    withCtx(
        [](AiChatContext &ctx)
        {
            EXPECT_EQ(ctx.aiIndex, 0);
            EXPECT_DOUBLE_EQ(ctx.temperature, 0.7);
            EXPECT_EQ(readCString(ctx.modelName), "m1");
        });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_InvalidAiIndex)
{
    registerServiceGateway();

    auto req = makeConfigReq(std::string(kValidPayload));
    req.aiIndex = 0x10;
    sendToMe(std::move(req));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });

    withCtx([](AiChatContext &ctx) { EXPECT_EQ(ctx.aiIndex, kInvalidAiIndex); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_MissingField)
{
    registerServiceGateway();

    sendToMe(makeConfigReq(R"({"apiUrl":"u","apiKey":"k","model":"m"})"));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });

    withCtx([](AiChatContext &ctx) { EXPECT_DOUBLE_EQ(ctx.temperature, 0.0); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_InvalidTemperature)
{
    registerServiceGateway();

    sendToMe(makeConfigReq(R"({"apiUrl":"u","apiKey":"k","model":"m","temperature":"hot"})"));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_ModelTooLong)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    std::string payload = std::string(R"({"apiUrl":"u","apiKey":"k","model":")") +
                          std::string(64, 'm') + std::string(R"(","temperature":0.5})");
    sendToMe(makeConfigReq(std::move(payload)));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_NoContext)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    auto req = makeConfigReq(std::string(kValidPayload));
    req.head.busTaskIds = {static_cast<common::GTID>(0xFFFF)};
    sendToMe(std::move(req));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_AfterConfig)
{
    registerServiceGateway();

    sendToMe(makeConfigReq(std::string(kValidPayload)));
    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_TRUE(msg.isSuccess); });

    sendToMe(makeChatReq());

    checkOutput<AiChatServiceReq>(serviceGatewayStub_,
                                  [](AiChatServiceReq &msg)
                                  {
                                      EXPECT_EQ(msg.reqSeq, 0);
                                      EXPECT_EQ(msg.modelName, "m1");
                                      EXPECT_DOUBLE_EQ(msg.temperature, 0.7);
                                      EXPECT_THAT(msg.messagesJson, testing::HasSubstr("system"));
                                      EXPECT_THAT(msg.messagesJson, testing::HasSubstr("user"));
                                  });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_NoContext)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    auto req = makeChatReq();
    req.head.busTaskIds = {static_cast<common::GTID>(0xFFFF)};
    sendToMe(std::move(req));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_FALSE(msg.success);
                                EXPECT_TRUE(msg.content.empty());
                                EXPECT_EQ(msg.aiIndex, kInvalidAiIndex);
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_InvalidMessagesJson)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    withCtx([](AiChatContext &ctx) { ctx.aiIndex = 3; });

    auto req = makeChatReq();
    req.messagesJson = "not-valid-json";
    sendToMe(std::move(req));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_FALSE(msg.success);
                                EXPECT_TRUE(msg.content.empty());
                                EXPECT_EQ(msg.aiIndex, 3);
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_InvalidMessagesJson_NotConfigured)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    auto req = makeChatReq();
    req.messagesJson = "not-valid-json";
    sendToMe(std::move(req));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_FALSE(msg.success);
                                EXPECT_TRUE(msg.content.empty());
                                EXPECT_EQ(msg.aiIndex, kInvalidAiIndex);
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_ValidJsonNotArray)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_)).Times(2);

    auto req = makeChatReq();
    req.messagesJson = "{}";
    sendToMe(std::move(req));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_FALSE(msg.success);
                                EXPECT_TRUE(msg.content.empty());
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatConfigReq_EmptyBusTaskIds)
{
    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    auto req = makeConfigReq(std::string(kValidPayload));
    req.head.busTaskIds.clear();
    sendToMe(std::move(req));

    checkOutput<AiChatConfigResp>(sessionDispatcherStub_,
                                  [](AiChatConfigResp &msg) { EXPECT_FALSE(msg.isSuccess); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatReq_EmptyBusTaskIds)
{
    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    auto req = makeChatReq();
    req.head.busTaskIds.clear();
    sendToMe(std::move(req));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_FALSE(msg.success);
                                EXPECT_TRUE(msg.content.empty());
                                EXPECT_EQ(msg.aiIndex, kInvalidAiIndex);
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_EmptyBusTaskIds)
{
    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    AiChatServiceResp resp;
    resp.head.sessionTaskId = gtid_;
    resp.success = true;
    resp.content = "answer";
    sendToMe(std::move(resp));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_Routes)
{
    registerServiceGateway();

    withCtx([](AiChatContext &ctx) { ctx.aiIndex = 3; });

    AiChatServiceResp resp;
    resp.head.sessionTaskId = gtid_;
    resp.head.busTaskIds = {gtid_};
    resp.success = true;
    resp.content = "answer";
    resp.reqSeq = 99;
    sendToMe(std::move(resp));

    checkOutput<AiChatResp>(sessionDispatcherStub_,
                            [](AiChatResp &msg)
                            {
                                EXPECT_TRUE(msg.success);
                                EXPECT_EQ(msg.aiIndex, 3);
                                EXPECT_EQ(msg.content, "answer");
                            });
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_NoContext)
{
    registerServiceGateway();

    EXPECT_CALL(mockSysLog_, log(utils::LogLevel::WRN, ::testing::_));

    AiChatServiceResp resp;
    resp.head.sessionTaskId = 0xFFFF;
    resp.head.busTaskIds = {static_cast<common::GTID>(0xFFFF)};
    resp.success = true;
    sendToMe(std::move(resp));
}

} // namespace
