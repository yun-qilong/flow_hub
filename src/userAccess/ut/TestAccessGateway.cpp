#include "fw/EoTestBase.hpp"
#include "userAccess/AccessGateway.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;
using utils::LogLevel;

class TestAccessGateway : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        cliAdapter_ = makeStub();
        sessionMgr_ = makeStub();
        sessionDispatcher_ = makeStub();
        trackStub(cliAdapter_);
        trackStub(sessionMgr_);
        trackStub(sessionDispatcher_);

        testee_ = spawn<userAccess::AccessGateway>(stubAddress(cliAdapter_));

        checkOutput<TempConfig>(cliAdapter_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 0); });

        sendToMeFrom(sessionMgr_, testee_, TempConfig{1});
        sendToMeFrom(sessionDispatcher_, testee_, TempConfig{2});
    }

    void sendBusinessResp(common::GTID sessionTaskId)
    {
        auto resp = AiChatBusinessResp{};
        fillDefaultHead(resp);
        resp.head.sessionTaskId = sessionTaskId;
        resp.content = "reply";
        sendToMe(std::move(resp));
    }

    void writeMapping(common::GTID sessionTaskId)
    {
        auto resp = TaskCreateResp{};
        fillDefaultHead(resp);
        resp.head.sessionTaskId = sessionTaskId;
        resp.isSuccess = true;
        resp.cookie.adapterAddr = stubAddress(cliAdapter_);
        sendToMe(std::move(resp));
        checkOutput<TaskCreateResp>(cliAdapter_, [](TaskCreateResp &) {});
    }

    Stub cliAdapter_;
    Stub sessionMgr_;
    Stub sessionDispatcher_;
};

TEST_F(TestAccessGateway, CheckHandleTempConfig_Tag1SetsSessionMgr)
{
    auto req = TaskCreateReq{};
    sendToMe(std::move(req));
    checkOutput<TaskCreateReq>(sessionMgr_, [](TaskCreateReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTempConfig_Tag2SetsSessionDispatcher)
{
    auto req = AiChatBusinessReq{};
    sendToMe(std::move(req));
    checkOutput<AiChatBusinessReq>(sessionDispatcher_, [](AiChatBusinessReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTempConfig_UnknownTagNoOp)
{
    sendToMeFrom(sessionMgr_, testee_, TempConfig{99});

    auto req = TaskCreateReq{};
    sendToMe(std::move(req));
    checkOutput<TaskCreateReq>(sessionMgr_, [](TaskCreateReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTaskCreateReq_FillsCookieAndForwards)
{
    auto req = TaskCreateReq{};
    fillDefaultHead(req);
    sendToMe(std::move(req));

    checkOutput<TaskCreateReq>(sessionMgr_, [&](TaskCreateReq &msg)
                               { EXPECT_EQ(msg.cookie.adapterAddr, stubAddress(cliAdapter_)); });
}

TEST_F(TestAccessGateway, CheckHandleTaskDeleteReq_ForwardsToSessionMgr)
{
    auto req = TaskDeleteReq{};
    fillDefaultHead(req);
    sendToMe(std::move(req));
    checkOutput<TaskDeleteReq>(sessionMgr_, [](TaskDeleteReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleAiChatBusinessReq_ForwardsToSessionDispatcher)
{
    auto req = AiChatBusinessReq{};
    req.content = "hello";
    sendToMe(std::move(req));
    checkOutput<AiChatBusinessReq>(sessionDispatcher_,
                                   [](AiChatBusinessReq &msg) { EXPECT_EQ(msg.content, "hello"); });
}

TEST_F(TestAccessGateway, CheckHandleTaskCreateResp_SuccessWritesMappingAndRoutes)
{
    constexpr common::GTID kGtid = 0x7001;

    writeMapping(kGtid);

    sendBusinessResp(kGtid);
    checkOutput<AiChatBusinessResp>(cliAdapter_, [](AiChatBusinessResp &msg)
                                    { EXPECT_EQ(msg.content, "reply"); });
}

TEST_F(TestAccessGateway, CheckHandleTaskCreateResp_FailureNoMapping)
{
    constexpr common::GTID kGtid = 0x7002;

    auto resp = TaskCreateResp{};
    fillDefaultHead(resp);
    resp.head.sessionTaskId = kGtid;
    resp.isSuccess = false;
    resp.cookie.adapterAddr = stubAddress(cliAdapter_);
    sendToMe(std::move(resp));
    checkOutput<TaskCreateResp>(cliAdapter_, [](TaskCreateResp &) {});

    EXPECT_LOG(LogLevel::ERR, 1);

    sendBusinessResp(kGtid);
}

TEST_F(TestAccessGateway, CheckHandleAiChatBusinessResp_NoMappingDrops)
{
    EXPECT_LOG(LogLevel::ERR, 1);

    sendBusinessResp(0x7777);
}

TEST_F(TestAccessGateway, CheckHandleTaskDeleteResp_ClearsMapping)
{
    constexpr common::GTID kGtid = 0x7003;

    writeMapping(kGtid);

    auto del = TaskDeleteResp{};
    fillDefaultHead(del);
    del.head.sessionTaskId = kGtid;
    del.isSuccess = true;
    sendToMe(std::move(del));
    checkOutput<TaskDeleteResp>(cliAdapter_, [](TaskDeleteResp &) {});

    EXPECT_LOG(LogLevel::ERR, 1);

    sendBusinessResp(kGtid);
}

} // namespace
