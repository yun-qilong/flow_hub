#include "common/TaskPool.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "fw/EoTestBase.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

namespace
{

using namespace common::message;
using utils::LogFeature;
using utils::LogLevel;
using AiChatBus = DPlane::business::AiChatBus<common::TaskType::AiAgora>;
using AiAgoraContext = common::context::AiAgoraContext;

class TestAiChatBus : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        sessionDataStub_ = makeStub();
        businessMgrStub_ = makeStub();
        routerStub_ = makeStub();
        serviceGatewayStub_ = makeStub();

        trackStub(sessionDataStub_);
        trackStub(businessMgrStub_);
        trackStub(routerStub_);
        trackStub(serviceGatewayStub_);

        pool_.allocate(common::TaskType::AiAgora)
            .useOrFailed([&](common::GTID g) { gtid_ = g; },
                         [] { FAIL() << "failed to allocate GTID"; });

        testee_ = spawn<AiChatBus>(pool_, stubAddress(sessionDataStub_),
                                   stubAddress(businessMgrStub_), stubAddress(routerStub_));

        checkOutput<TempConfig>(routerStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 6); });
    }

    template <typename F>
    void withCtx(F &&fn)
    {
        pool_.getContext<AiAgoraContext>(gtid_).useOrFailed([&](AiAgoraContext &ctx) { fn(ctx); },
                                                            [] { FAIL() << "context not found"; });
    }

    template <typename M>
    void fillHead(M &msg, common::GTID gtid)
    {
        msg.head.uid = kDefaultUid;
        msg.head.accessType = kDefaultAccessType;
        msg.head.appType = kDefaultAppType;
        msg.head.gtidList = {gtid};
    }

    void registerServiceGateway()
    {
        sendToMeFrom(serviceGatewayStub_, testee_, TempConfig{9});
    }

    common::TaskPool pool_;
    common::GTID gtid_ = 0;

    Stub sessionDataStub_;
    Stub businessMgrStub_;
    Stub routerStub_;
    Stub serviceGatewayStub_;
};

TEST_F(TestAiChatBus, CheckHandleAiChatBusinessReq_FullFlow)
{
    registerServiceGateway();

    EXPECT_LOG_FEAT(LogFeature::AICHAT, 3);

    AiChatBusinessReq req;
    fillHead(req, gtid_);
    req.content = "hello";
    sendToMe(std::move(req));

    checkOutput<AiChatServiceReq>(serviceGatewayStub_,
                                  [](AiChatServiceReq &msg)
                                  {
                                      EXPECT_EQ(msg.reqSeq, 1);
                                      EXPECT_THAT(msg.messagesJson, testing::HasSubstr("hello"));
                                      EXPECT_THAT(msg.messagesJson, testing::HasSubstr("system"));
                                      EXPECT_THAT(msg.messagesJson, testing::HasSubstr("user"));
                                  });
}

TEST_F(TestAiChatBus, CheckHandleAiChatBusinessReq_NoContext)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatBusinessReq req;
    fillHead(req, static_cast<common::GTID>(0xFFFF));
    req.content = "hello";
    sendToMe(std::move(req));
}

TEST_F(TestAiChatBus, CheckHandleAiChatBusinessReq_NoServiceGateway)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 2);
    EXPECT_LOG(LogLevel::ERR, 1);

    AiChatBusinessReq req;
    fillHead(req, gtid_);
    req.content = "hello";
    sendToMe(std::move(req));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_NormalResp)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);

    withCtx(
        [](AiAgoraContext &ctx)
        {
            ctx.pendingReqSeq = 1;
            ctx.messageCount = 1;
            ctx.messageOffsets.at(0) = 100;
            ctx.messagesLen = 100;
        });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = true;
    resp.content = "assistant reply";
    resp.reqSeq = 1;
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(sessionDataStub_,
                                    [](AiChatBusinessResp &msg)
                                    {
                                        EXPECT_TRUE(msg.success);
                                        EXPECT_EQ(msg.content, "assistant reply");
                                    });
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_NoPendingReq)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    withCtx([](AiAgoraContext &ctx) { ctx.pendingReqSeq = 0; });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = true;
    resp.reqSeq = 1;
    sendToMe(std::move(resp));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_StaleSeq)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);
    EXPECT_LOG(LogLevel::WRN, 1);

    withCtx([](AiAgoraContext &ctx) { ctx.pendingReqSeq = 5; });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = true;
    resp.reqSeq = 3;
    sendToMe(std::move(resp));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_Failure)
{
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);

    withCtx(
        [](AiAgoraContext &ctx)
        {
            ctx.pendingReqSeq = 1;
            ctx.messageCount = 1;
            ctx.messagesLen = 100;
        });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = false;
    resp.content = "error occurred";
    resp.reqSeq = 1;
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(sessionDataStub_,
                                    [](AiChatBusinessResp &msg)
                                    {
                                        EXPECT_FALSE(msg.success);
                                        EXPECT_EQ(msg.content, "error occurred");
                                    });
}

} // namespace
