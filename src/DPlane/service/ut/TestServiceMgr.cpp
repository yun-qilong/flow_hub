#include "DPlane/service/ServiceMgr.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

class TestServiceMgr : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        businessMgrStub_ = makeStub();
        serviceGatewayStub_ = makeStub();
        aiApiAdapterStub_ = makeStub();

        trackStub(businessMgrStub_);
        trackStub(serviceGatewayStub_);
        trackStub(aiApiAdapterStub_);

        testee_ = spawn<DPlane::service::ServiceMgr>(stubAddress(businessMgrStub_));

        checkOutput<TempConfig>(businessMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 7); });
    }

    Stub businessMgrStub_;
    Stub serviceGatewayStub_;
    Stub aiApiAdapterStub_;
};

TEST_F(TestServiceMgr, CheckHandleTempConfig_Tag8SetsServiceGatewayAddr)
{
    sendToMeFrom(serviceGatewayStub_, testee_, TempConfig{8});
}

TEST_F(TestServiceMgr, CheckHandleTempConfig_Tag11SetsAiApiAdapterAddr)
{
    sendToMeFrom(aiApiAdapterStub_, testee_, TempConfig{11});
}

TEST_F(TestServiceMgr, CheckHandleTempConfig_UnknownTagNoOp)
{
    sendToMe(TempConfig{0});
}

} // namespace
