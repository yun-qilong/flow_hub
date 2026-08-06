#include "DPlane/business/Router.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace
{

using namespace common::message;
using utils::LogLevel;

constexpr common::GTID kGtidIdx0 = 0x0001;
constexpr common::GTID kGtidIdx1 = 0x0041;
constexpr common::GTID kGtidIdx2 = 0x0081;
constexpr common::GTID kAiChatGtid =
    static_cast<common::GTID>((static_cast<uint16_t>(common::TaskType::AiChat) << 6) | 1);

class TestRouter : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        businessMgrStub_ = makeStub();
        sessionDispatcherStub_ = makeStub();
        businessStubA_ = makeStub();
        businessStubB_ = makeStub();
        businessStubC_ = makeStub();

        trackStub(businessMgrStub_);
        trackStub(sessionDispatcherStub_);
        trackStub(businessStubA_);
        trackStub(businessStubB_);
        trackStub(businessStubC_);

        testee_ = spawn<DPlane::business::Router>(stubAddress(businessMgrStub_),
                                                  stubAddress(sessionDispatcherStub_));

        checkOutput<TempConfig>(businessMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 5); });
        checkOutput<TempConfig>(sessionDispatcherStub_,
                                [](TempConfig &msg) { EXPECT_EQ(msg.tag, 3); });
    }

    template <typename M>
    void fillHead(M &msg, std::vector<common::GTID> busTaskIds)
    {
        msg.head.sessionTaskId = kDefaultGtid;
        msg.head.busTaskIds = std::move(busTaskIds);
    }

    AiChatReq makeChatReq(std::vector<common::GTID> busTaskIds)
    {
        AiChatReq req;
        fillHead(req, std::move(busTaskIds));
        req.messagesJson = R"([{"role":"user","content":"hi"}])";
        return req;
    }

    Stub businessMgrStub_;
    Stub sessionDispatcherStub_;
    Stub businessStubA_;
    Stub businessStubB_;
    Stub businessStubC_;
};

TEST_F(TestRouter, CheckHandleTempConfig_Tag6RegistersRoute)
{
    sendToMeFrom(businessStubA_, testee_, TempConfig{6});

    EXPECT_LOG(LogLevel::DBG, 2);

    auto req = makeChatReq({kAiChatGtid});
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(businessStubA_,
                           [](AiChatReq &msg)
                           {
                               ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                               EXPECT_EQ(msg.head.busTaskIds.at(0), kAiChatGtid);
                           });
}

TEST_F(TestRouter, CheckHandleRouterConfigReq_InstallAndReply)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    sendToMe(std::move(cfg));

    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    auto req = makeChatReq({kGtidIdx0});
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(businessStubA_,
                           [](AiChatReq &msg) { EXPECT_EQ(msg.head.busTaskIds.size(), 1u); });
}

TEST_F(TestRouter, CheckHandleRouterReconfigReq_UpdateAndReply)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterReconfigReq recfg;
    recfg.entries.push_back({common::TaskType::Service, stubAddress(businessStubA_)});
    sendToMe(std::move(recfg));

    checkOutput<RouterReconfigResp>([](RouterReconfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    auto req = makeChatReq({kGtidIdx0});
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(businessStubA_,
                           [](AiChatReq &msg) { EXPECT_EQ(msg.head.busTaskIds.size(), 1u); });
}

TEST_F(TestRouter, CheckRouteAndForward_SingleBusTaskId)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    auto req = makeChatReq({kGtidIdx0});
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(businessStubA_,
                           [](AiChatReq &msg)
                           {
                               ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                               EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx0);
                           });
}

TEST_F(TestRouter, CheckRouteAndForward_MultipleBusTaskIds)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    cfg.addresses.at(1) = stubAddress(businessStubB_);
    cfg.addresses.at(2) = stubAddress(businessStubC_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 4);

    auto req = makeChatReq({kGtidIdx0, kGtidIdx1, kGtidIdx2});
    sendToMe(std::move(req));

    checkOutput<AiChatReq>(businessStubA_,
                           [](AiChatReq &msg)
                           {
                               ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                               EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx0);
                           });
    checkOutput<AiChatReq>(businessStubB_,
                           [](AiChatReq &msg)
                           {
                               ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                               EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx1);
                           });
    checkOutput<AiChatReq>(businessStubC_,
                           [](AiChatReq &msg)
                           {
                               ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                               EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx2);
                           });
}

TEST_F(TestRouter, CheckHandleAiChatConfigReq_FanOut)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    cfg.addresses.at(1) = stubAddress(businessStubB_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 3);

    AiChatConfigReq req;
    fillHead(req, {kGtidIdx0, kGtidIdx1});
    req.aiIndex = 0;
    req.systemPrompt = "sys";
    req.payload = R"({"apiUrl":"u","apiKey":"k","model":"m","temperature":0.5})";
    sendToMe(std::move(req));

    checkOutput<AiChatConfigReq>(businessStubA_,
                                 [](AiChatConfigReq &msg)
                                 {
                                     ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                                     EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx0);
                                 });
    checkOutput<AiChatConfigReq>(businessStubB_,
                                 [](AiChatConfigReq &msg)
                                 {
                                     ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                                     EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx1);
                                 });
}

TEST_F(TestRouter, CheckRouteAndForward_EmptyBusTaskIds)
{
    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatServiceResp resp;
    fillHead(resp, {});
    resp.success = true;
    sendToMe(std::move(resp));
}

TEST_F(TestRouter, CheckRouteAndForward_LastBusTaskIdNoRoute)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatServiceResp resp;
    fillHead(resp, {kGtidIdx0, static_cast<uint16_t>(0xFFFF)});
    resp.success = true;
    sendToMe(std::move(resp));

    checkOutput<AiChatServiceResp>(businessStubA_,
                                   [](AiChatServiceResp &msg) { EXPECT_TRUE(msg.success); });
}

TEST_F(TestRouter, CheckHandleAiChatServiceResp_Routes)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    AiChatServiceResp resp;
    fillHead(resp, {kGtidIdx0});
    resp.success = true;
    sendToMe(std::move(resp));

    checkOutput<AiChatServiceResp>(businessStubA_,
                                   [](AiChatServiceResp &msg)
                                   {
                                       EXPECT_TRUE(msg.success);
                                       ASSERT_EQ(msg.head.busTaskIds.size(), 1u);
                                       EXPECT_EQ(msg.head.busTaskIds.at(0), kGtidIdx0);
                                   });
}

} // namespace
