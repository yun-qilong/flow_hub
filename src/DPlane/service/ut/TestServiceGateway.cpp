#include "DPlane/service/ServiceGateway.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

using namespace common::message;

class TestServiceGateway : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        serviceMgrStub_ = makeStub();
        aiChatBusStub_ = makeStub();
        aiApiAdapterStub_ = makeStub();

        trackStub(serviceMgrStub_);
        trackStub(aiChatBusStub_);
        trackStub(aiApiAdapterStub_);

        testee_ = spawn<DPlane::service::ServiceGateway>(stubAddress(serviceMgrStub_),
                                                         stubAddress(aiChatBusStub_));

        checkOutput<TempConfig>(serviceMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 8); });
        checkOutput<TempConfig>(aiChatBusStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 9); });
    }

    Stub serviceMgrStub_;
    Stub aiChatBusStub_;
    Stub aiApiAdapterStub_;
};

TEST_F(TestServiceGateway, CheckHandleTempConfig_Tag10RegistersAdapter)
{
    sendToMeFrom(aiApiAdapterStub_, testee_, TempConfig{10});

    AiChatServiceReq req;
    req.modelName = "gpt-4";
    sendToMe(std::move(req));

    checkOutput<AiChatServiceReq>(aiApiAdapterStub_,
                                  [](AiChatServiceReq &msg) { EXPECT_EQ(msg.modelName, "gpt-4"); });
}

TEST_F(TestServiceGateway, CheckHandleAiChatServiceReq_ForwardsToAdapter)
{
    sendToMeFrom(aiApiAdapterStub_, testee_, TempConfig{10});

    AiChatServiceReq req;
    req.modelName = "claude-3";
    req.temperature = 0.7;
    req.messagesJson = R"([{"role":"user","content":"hello"}])";
    req.reqSeq = 42;
    sendToMe(std::move(req));

    checkOutput<AiChatServiceReq>(aiApiAdapterStub_,
                                  [](AiChatServiceReq &msg)
                                  {
                                      EXPECT_EQ(msg.modelName, "claude-3");
                                      EXPECT_DOUBLE_EQ(msg.temperature, 0.7);
                                      EXPECT_EQ(msg.messagesJson,
                                                R"([{"role":"user","content":"hello"}])");
                                      EXPECT_EQ(msg.reqSeq, 42);
                                  });
}

TEST_F(TestServiceGateway, CheckHandleAiChatServiceReq_AdapterNotSet)
{
    AiChatServiceReq req;
    req.modelName = "gpt-4";
    sendToMe(std::move(req));
}

} // namespace
