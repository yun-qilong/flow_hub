#include "common/TaskPool.hpp"
#include "DPlane/business/AiChatBus.hpp"
#include "fw/EoTestBase.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

namespace
{

using namespace common::message;
using AiChatBus = DPlane::business::AiChatBus<common::TaskType::AiChat>;
using AiChatContext = common::context::AiChatContext;

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

        pool_.allocate(common::TaskType::AiChat)
            .useOrFailed([&](common::GTID g) { gtid_ = g; },
                         [] { FAIL() << "failed to allocate GTID"; });

        testee_ = spawn<AiChatBus>(pool_, stubAddress(sessionDataStub_),
                                   stubAddress(businessMgrStub_), stubAddress(routerStub_));

        checkOutput<TempConfig>(routerStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 6); });
    }

    template <typename F>
    void withCtx(F &&fn)
    {
        pool_.getContext<AiChatContext>(gtid_).useOrFailed([&](AiChatContext &ctx) { fn(ctx); },
                                                           [] { FAIL() << "context not found"; });
    }

    template <typename M>
    void fillHead(M &msg, common::GTID gtid)
    {
        msg.head.uid = kDefaultUid;
        msg.head.accessType = kDefaultAccessType;
        msg.head.appType = kDefaultAppType;
        msg.head.sessionFlags = {};
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

    AiChatBusinessReq req;
    fillHead(req, gtid_);
    req.content = "hello";
    sendToMe(std::move(req));

    checkOutput<AiChatMsgAck>(sessionDataStub_,
                              [](AiChatMsgAck &msg)
                              {
                                  EXPECT_EQ(msg.seq, 1);
                                  EXPECT_EQ(msg.content, "hello");
                              });

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
    AiChatBusinessReq req;
    fillHead(req, static_cast<common::GTID>(0xFFFF));
    req.content = "hello";
    sendToMe(std::move(req));
}

TEST_F(TestAiChatBus, CheckHandleAiChatBusinessReq_NoServiceGateway)
{
    AiChatBusinessReq req;
    fillHead(req, gtid_);
    req.content = "hello";
    sendToMe(std::move(req));

    checkOutput<AiChatMsgAck>(sessionDataStub_, [](AiChatMsgAck &msg) { EXPECT_EQ(msg.seq, 1); });
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_NormalResp)
{
    withCtx(
        [](AiChatContext &ctx)
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
    withCtx([](AiChatContext &ctx) { ctx.pendingReqSeq = 0; });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = true;
    resp.reqSeq = 1;
    sendToMe(std::move(resp));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_StaleSeq)
{
    withCtx([](AiChatContext &ctx) { ctx.pendingReqSeq = 5; });

    AiChatServiceResp resp;
    fillHead(resp, gtid_);
    resp.success = true;
    resp.reqSeq = 3;
    sendToMe(std::move(resp));
}

TEST_F(TestAiChatBus, CheckHandleAiChatServiceResp_Failure)
{
    withCtx(
        [](AiChatContext &ctx)
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
