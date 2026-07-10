#include "CPlane/BusinessMgr.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

class TestBusinessMgr : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        sessionMgrStub_ = makeStub();
        routerStub_ = makeStub();
        serviceMgrStub_ = makeStub();
        trackStub(sessionMgrStub_);
        trackStub(routerStub_);
        trackStub(serviceMgrStub_);

        testee_ = spawn<CPlane::BusinessMgr>(stubAddress(sessionMgrStub_));

        checkOutput<TempConfig>(sessionMgrStub_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 4); });
    }

    Stub sessionMgrStub_;
    Stub routerStub_;
    Stub serviceMgrStub_;
};

TEST_F(TestBusinessMgr, CheckHandleTempConfig_Tag5SetsRouterAddr)
{
    sendToMeFrom(routerStub_, testee_, TempConfig{5});
}

TEST_F(TestBusinessMgr, CheckHandleTempConfig_Tag7SetsServiceMgrAddr)
{
    sendToMeFrom(serviceMgrStub_, testee_, TempConfig{7});
}

TEST_F(TestBusinessMgr, CheckHandleTempConfig_UnknownTagNoOp)
{
    sendToMe(TempConfig{0});
}

} // namespace
