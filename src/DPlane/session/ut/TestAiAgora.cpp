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
        testee_ = spawn<DPlane::session::AiAgora>();
    }
};

TEST_F(TestAiAgora, CheckHandleTempConfig_NoOp)
{
    sendToMe(TempConfig{0});
}

} // namespace
