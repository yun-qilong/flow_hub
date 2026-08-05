#include "fw/EoTestBase.hpp"
#include "userAccess/AccessGateway.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

// ===== base fixture =====

class TestAccessGateway : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        cliAdapter_ = makeStub();
        sessionMgr_ = makeStub();
        sessionData_ = makeStub();
        trackStub(cliAdapter_);
        trackStub(sessionMgr_);
        trackStub(sessionData_);

        testee_ = spawn<userAccess::AccessGateway>(stubAddress(cliAdapter_));

        checkOutput<TempConfig>(cliAdapter_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 0); });

        sendToMeFrom(sessionMgr_, testee_, TempConfig{1});
        sendToMeFrom(sessionData_, testee_, TempConfig{2});
    }

    Stub cliAdapter_;
    Stub sessionMgr_;
    Stub sessionData_;

    static constexpr common::AccessType kCliAccessType = static_cast<common::AccessType>(0);
    static constexpr common::AccessType kEmptyAccessType = static_cast<common::AccessType>(1);
};

// ===== TempConfig =====

TEST_F(TestAccessGateway, CheckHandleTempConfig_Tag1SetsSessionMgr)
{
    auto req = TaskCreateReq{};
    sendToMe(std::move(req));
    checkOutput<TaskCreateReq>(sessionMgr_, [](TaskCreateReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTempConfig_Tag2SetsSessionData)
{
    auto req = AiChatBusinessReq{};
    sendToMe(std::move(req));
    checkOutput<AiChatBusinessReq>(sessionData_, [](AiChatBusinessReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTempConfig_UnknownTagNoOp)
{
    sendToMeFrom(sessionMgr_, testee_, TempConfig{99});

    auto req = TaskCreateReq{};
    sendToMe(std::move(req));
    checkOutput<TaskCreateReq>(sessionMgr_, [](TaskCreateReq &) {});
}

// ===== AiChatBusinessReq → sessionData =====

TEST_F(TestAccessGateway, CheckHandleAiChatBusinessReq_DelegateToSessionData)
{
    auto req = AiChatBusinessReq{};
    req.content = "hello";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(sessionData_,
                                   [](AiChatBusinessReq &msg) { EXPECT_EQ(msg.content, "hello"); });
}

// ===== TYPED_TEST: Req → sessionMgr =====

template <typename ReqMsg>
class TestAccessGateway_ReqToSessionMgr : public TestAccessGateway
{
};

using ReqMsgTypes = testing::Types<TaskCreateReq, TaskDeleteReq>;

TYPED_TEST_SUITE(TestAccessGateway_ReqToSessionMgr, ReqMsgTypes);

TYPED_TEST(TestAccessGateway_ReqToSessionMgr, DelegatesToSessionMgr)
{
    TypeParam req{};
    this->sendToMe(std::move(req));
    this->template checkOutput<TypeParam>(this->sessionMgr_, [](TypeParam &) {});
}

// ===== TYPED_TEST: Resp → forwardToAdapter =====

template <typename Msg>
class TestAccessGateway_RespForwardToAdapter : public TestAccessGateway
{
};

using RespMsgTypes = testing::Types<TaskCreateResp, TaskDeleteResp, AiChatBusinessResp>;

TYPED_TEST_SUITE(TestAccessGateway_RespForwardToAdapter, RespMsgTypes);

TYPED_TEST(TestAccessGateway_RespForwardToAdapter, ForwardsToAdapter)
{
    TypeParam msg{};
    msg.head.accessType = this->kCliAccessType;
    msg.head.targets = 0;
    this->sendToMe(std::move(msg));
    this->template checkOutput<TypeParam>(this->cliAdapter_, [](TypeParam &) {});
}

} // namespace
