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
    static_cast<common::GTID>((static_cast<uint16_t>(common::TaskType::AiAgora) << 6) | 1);

class TestRouter : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        businessMgrStub_ = makeStub();
        sessionDataStub_ = makeStub();
        businessStubA_ = makeStub();
        businessStubB_ = makeStub();
        businessStubC_ = makeStub();

        trackStub(businessMgrStub_);
        trackStub(sessionDataStub_);
        trackStub(businessStubA_);
        trackStub(businessStubB_);
        trackStub(businessStubC_);

        testee_ = spawn<DPlane::business::Router>(stubAddress(businessMgrStub_),
                                                  stubAddress(sessionDataStub_));

        checkOutput<TempConfig>(businessMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 5); });
        checkOutput<TempConfig>(sessionDataStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 3); });
    }

    template <typename M>
    void fillHead(M &msg, std::vector<common::GTID> gtidList)
    {
        msg.head.uid = kDefaultUid;
        msg.head.accessType = kDefaultAccessType;
        msg.head.appType = kDefaultAppType;
        msg.head.sessionFlags = {};
        msg.head.gtidList = std::move(gtidList);
    }

    Stub businessMgrStub_;
    Stub sessionDataStub_;
    Stub businessStubA_;
    Stub businessStubB_;
    Stub businessStubC_;
};

TEST_F(TestRouter, CheckHandleTempConfig_Tag6RegistersRoute)
{
    sendToMeFrom(businessStubA_, testee_, TempConfig{6});

    EXPECT_LOG(LogLevel::DBG, 2);

    AiChatBusinessReq req;
    fillHead(req, {kAiChatGtid});
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(businessStubA_,
                                   [](AiChatBusinessReq &msg)
                                   {
                                       EXPECT_EQ(msg.head.gtidList.size(), 1u);
                                       EXPECT_EQ(msg.head.gtidList.at(0), kAiChatGtid);
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

    AiChatBusinessReq req;
    fillHead(req, {kGtidIdx0});
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(businessStubA_, [](AiChatBusinessReq &msg)
                                   { EXPECT_EQ(msg.head.gtidList.size(), 1u); });
}

TEST_F(TestRouter, CheckHandleRouterReconfigReq_UpdateAndReply)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterReconfigReq recfg;
    recfg.entries.push_back({common::TaskType::Service, stubAddress(businessStubA_)});
    sendToMe(std::move(recfg));

    checkOutput<RouterReconfigResp>([](RouterReconfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    AiChatBusinessReq req;
    fillHead(req, {kGtidIdx0});
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(businessStubA_, [](AiChatBusinessReq &msg)
                                   { EXPECT_EQ(msg.head.gtidList.size(), 1u); });
}

TEST_F(TestRouter, CheckRouteAndForward_SingleGtid)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 2);

    AiChatBusinessReq req;
    fillHead(req, {kGtidIdx0});
    req.content = "test";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(businessStubA_,
                                   [](AiChatBusinessReq &msg) { EXPECT_EQ(msg.content, "test"); });
}

TEST_F(TestRouter, CheckRouteAndForward_MultipleGtids)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    RouterConfigReq cfg;
    cfg.addresses.at(0) = stubAddress(businessStubA_);
    cfg.addresses.at(1) = stubAddress(businessStubB_);
    cfg.addresses.at(2) = stubAddress(businessStubC_);
    sendToMe(std::move(cfg));
    checkOutput<RouterConfigResp>([](RouterConfigResp &msg) { EXPECT_TRUE(msg.success); });

    EXPECT_LOG(LogLevel::DBG, 4);

    AiChatBusinessReq req;
    fillHead(req, {kGtidIdx0, kGtidIdx1, kGtidIdx2});
    req.content = "fanout";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(businessStubA_, [](AiChatBusinessReq &msg)
                                   { EXPECT_EQ(msg.content, "fanout"); });
    checkOutput<AiChatBusinessReq>(businessStubB_, [](AiChatBusinessReq &msg)
                                   { EXPECT_EQ(msg.content, "fanout"); });
    checkOutput<AiChatBusinessReq>(businessStubC_, [](AiChatBusinessReq &msg)
                                   { EXPECT_EQ(msg.content, "fanout"); });
}

TEST_F(TestRouter, CheckRouteAndForward_EmptyGtidList)
{
    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatServiceResp resp;
    fillHead(resp, {});
    resp.success = true;
    sendToMe(std::move(resp));
}

TEST_F(TestRouter, CheckRouteAndForward_LastGtidNoRoute)
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
                                       EXPECT_EQ(msg.head.gtidList.size(), 1u);
                                   });
}

} // namespace
