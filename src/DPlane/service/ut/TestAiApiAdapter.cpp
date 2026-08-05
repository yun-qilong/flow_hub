#include "DPlane/service/AiApiAdapter.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;
using utils::LogLevel;

class TestAiApiAdapter : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        routerStub_ = makeStub();
        serviceMgrStub_ = makeStub();
        serviceGatewayStub_ = makeStub();

        trackStub(routerStub_);
        trackStub(serviceMgrStub_);
        trackStub(serviceGatewayStub_);

        testee_ = spawn<DPlane::service::AiApiAdapter>(
            "http://127.0.0.1:1", "fake-key", "gpt-4", stubAddress(routerStub_),
            stubAddress(serviceMgrStub_), stubAddress(serviceGatewayStub_));

        checkOutput<TempConfig>(serviceGatewayStub_,
                                [](TempConfig &msg) { EXPECT_EQ(msg.tag, 10); });
        checkOutput<TempConfig>(serviceMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 11); });
    }

    template <typename M>
    void fillHead(M &msg)
    {
        msg.head.sessionTaskId = kDefaultGtid;
        msg.head.busTaskIds = {kDefaultGtid};
    }

    Stub routerStub_;
    Stub serviceMgrStub_;
    Stub serviceGatewayStub_;
};

TEST_F(TestAiApiAdapter, CheckHandleAiChatServiceReq_SendsRespOnHttpFailure)
{
    EXPECT_LOG(LogLevel::DBG, 2);

    AiChatServiceReq req;
    fillHead(req);
    req.messagesJson = "[]";
    req.modelName = "gpt-4";
    sendToMe(std::move(req));

    checkOutput<AiChatServiceResp>(routerStub_,
                                   [](AiChatServiceResp &msg) { EXPECT_FALSE(msg.success); });
}

TEST_F(TestAiApiAdapter, CheckHandleAiChatServiceReq_RouterNotSet)
{
    auto adapter = spawn<DPlane::service::AiApiAdapter>(
        "http://127.0.0.1:1", "fake-key", "gpt-4", fw::EoAddress{}, stubAddress(serviceMgrStub_),
        stubAddress(serviceGatewayStub_));

    checkOutput<TempConfig>(serviceGatewayStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 10); });
    checkOutput<TempConfig>(serviceMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 11); });

    EXPECT_LOG(LogLevel::DBG, 2);
    EXPECT_LOG(LogLevel::WRN, 1);

    AiChatServiceReq req;
    fillHead(req);
    req.messagesJson = "[]";
    req.modelName = "gpt-4";
    sendToMeFrom(stubEo_, adapter, std::move(req));

    stopActor(adapter);
}

TEST_F(TestAiApiAdapter, CheckHandleApiKeyUpdate_NoOutput)
{
    EXPECT_LOG(LogLevel::INFO, 1);

    sendToMe(ApiKeyUpdate{"new-key"});
}

} // namespace
