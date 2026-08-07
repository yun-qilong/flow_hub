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
        auto resp = AiAgoraChatResp{};
        fillDefaultHead(resp);
        resp.head.sessionTaskId = sessionTaskId;
        resp.responses = "reply";
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

    void writeMappingTo(common::GTID sessionTaskId, Stub &adapter)
    {
        auto resp = TaskCreateResp{};
        fillDefaultHead(resp);
        resp.head.sessionTaskId = sessionTaskId;
        resp.isSuccess = true;
        resp.cookie.adapterAddr = stubAddress(adapter);
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
    auto req = AiAgoraChatReq{};
    sendToMe(std::move(req));
    checkOutput<AiAgoraChatReq>(sessionDispatcher_, [](AiAgoraChatReq &) {});
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

TEST_F(TestAccessGateway, CheckHandleTaskDeleteReq_ForwardsToSessionDispatcher)
{
    auto req = TaskDeleteReq{};
    fillDefaultHead(req);
    sendToMe(std::move(req));
    checkOutput<TaskDeleteReq>(sessionDispatcher_, [](TaskDeleteReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleAiAgoraChatReq_ForwardsToSessionDispatcher)
{
    auto req = AiAgoraChatReq{};
    req.content = "hello";
    sendToMe(std::move(req));
    checkOutput<AiAgoraChatReq>(sessionDispatcher_,
                                [](AiAgoraChatReq &msg) { EXPECT_EQ(msg.content, "hello"); });
}

TEST_F(TestAccessGateway, CheckHandleTaskCreateResp_SuccessWritesMappingAndRoutes)
{
    constexpr common::GTID kGtid = 0x7001;

    writeMapping(kGtid);

    sendBusinessResp(kGtid);
    checkOutput<AiAgoraChatResp>(cliAdapter_,
                                 [](AiAgoraChatResp &msg) { EXPECT_EQ(msg.responses, "reply"); });
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

TEST_F(TestAccessGateway, CheckHandleAiAgoraChatResp_NoMappingDrops)
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

TEST_F(TestAccessGateway, CheckMapping_SameGtidOverwritesToNewAdapter)
{
    auto adapterB = makeStub();

    writeMapping(0x7123);
    writeMappingTo(0x7123, adapterB);

    sendBusinessResp(0x7123);
    checkOutput<AiAgoraChatResp>(adapterB, [](AiAgoraChatResp &) {});
}

TEST_F(TestAccessGateway, CheckMapping_IndexBoundaryLow)
{
    writeMapping(0x0000);

    sendBusinessResp(0x0000);
    checkOutput<AiAgoraChatResp>(cliAdapter_, [](AiAgoraChatResp &) {});
}

TEST_F(TestAccessGateway, CheckMapping_IndexBoundaryHigh)
{
    writeMapping(0x0FFF);

    sendBusinessResp(0x0FFF);
    checkOutput<AiAgoraChatResp>(cliAdapter_, [](AiAgoraChatResp &) {});
}

TEST_F(TestAccessGateway, CheckMapping_HighBitsMaskedToSameSlot)
{
    writeMapping(0x1000);

    auto del = TaskDeleteResp{};
    fillDefaultHead(del);
    del.head.sessionTaskId = 0x0000;
    del.isSuccess = true;
    sendToMe(std::move(del));
    checkOutput<TaskDeleteResp>(cliAdapter_, [](TaskDeleteResp &) {});

    EXPECT_LOG(LogLevel::ERR, 1);

    sendBusinessResp(0x1000);
}

TEST_F(TestAccessGateway, CheckMapping_DeleteUnmappedIdempotent)
{
    auto del = TaskDeleteResp{};
    fillDefaultHead(del);
    del.head.sessionTaskId = 0x7777;
    del.isSuccess = true;
    sendToMe(std::move(del));
    checkOutput<TaskDeleteResp>(cliAdapter_, [](TaskDeleteResp &) {});

    auto delAgain = TaskDeleteResp{};
    fillDefaultHead(delAgain);
    delAgain.head.sessionTaskId = 0x7777;
    delAgain.isSuccess = true;
    sendToMe(std::move(delAgain));
    checkOutput<TaskDeleteResp>(cliAdapter_, [](TaskDeleteResp &) {});
}

TEST_F(TestAccessGateway, CheckHandleAiAgoraResetReq_ForwardsToSessionDispatcher)
{
    auto req = AiAgoraResetReq{};
    fillDefaultHead(req);
    sendToMe(std::move(req));
    checkOutput<AiAgoraResetReq>(sessionDispatcher_, [](AiAgoraResetReq &) {});
}

TEST_F(TestAccessGateway, CheckHandleTaskConfigReq_ForwardsToSessionDispatcher)
{
    auto req = TaskConfigReq{};
    fillDefaultHead(req);
    req.payload = "{}";
    sendToMe(std::move(req));
    checkOutput<TaskConfigReq>(sessionDispatcher_,
                               [](TaskConfigReq &msg) { EXPECT_EQ(msg.payload, "{}"); });
}

TEST_F(TestAccessGateway, CheckHandleAiAgoraResetResp_RoutesToAdapter)
{
    constexpr common::GTID kGtid = 0x7005;

    writeMapping(kGtid);

    auto resp = AiAgoraResetResp{};
    fillDefaultHead(resp);
    resp.head.sessionTaskId = kGtid;
    resp.isSuccess = true;
    resp.estimatedTopicCount = 42;
    sendToMe(std::move(resp));

    checkOutput<AiAgoraResetResp>(cliAdapter_, [](AiAgoraResetResp &msg)
                                  { EXPECT_EQ(msg.estimatedTopicCount, 42); });
}

} // namespace
