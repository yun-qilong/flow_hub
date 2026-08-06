#include "DPlane/session/AiAgora.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

class TestAiAgora : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        sessionDispatcher_ = makeStub();
        trackStub(sessionDispatcher_);

        testee_ = spawn<DPlane::session::AiAgora>(stubAddress(sessionDispatcher_));

        checkOutput<TempConfig>(sessionDispatcher_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 7); });
    }

    Stub sessionDispatcher_;
};

TEST_F(TestAiAgora, CheckHandleTempConfig_NoOp)
{
    sendToMe(TempConfig{0});
}

} // namespace
