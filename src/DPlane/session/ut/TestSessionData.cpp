#include "DPlane/session/SessionData.hpp"
#include "fw/EoTestBase.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;
using utils::LogLevel;

constexpr uint16_t kSeqMsgAck = 42;

class TestSessionData : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        accessGateway_ = makeStub();
        router_ = makeStub();
        trackStub(accessGateway_);
        trackStub(router_);

        testee_ = spawn<DPlane::session::SessionData>(stubAddress(accessGateway_));

        sendAndVerifyTempConfigInConstructor();
        setRouterAddr();
    }

    void sendAndVerifyTempConfigInConstructor()
    {
        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 2); });
    }

    void setRouterAddr()
    {
        sendToMeFrom(router_, testee_, TempConfig{3});
    }

    void setAccessBit(uint16_t uid, uint64_t accessBits)
    {
        while (accessBits)
        {
            auto bit = __builtin_ctzll(accessBits);
            accessBits &= (accessBits - 1);
            auto req = UserLoginSessionReq{};
            req.head.uid = uid;
            req.head.accessType = static_cast<common::AccessType>(bit);
            sendToMeFrom(router_, testee_, std::move(req));
            checkOutput<UserLoginSessionResp>(router_, [](UserLoginSessionResp &) {});
        }
    }

    void restartWithoutRouter()
    {
        stopActor(testee_);
        testee_ = spawn<DPlane::session::SessionData>(stubAddress(accessGateway_));
        sendAndVerifyTempConfigInConstructor();
    }

    Stub accessGateway_;
    Stub router_;

    void checkHeadFields(const UserHead &head, uint16_t uid, common::AccessType accessType,
                         common::AppType appType, const std::vector<common::GTID> &gtidList,
                         uint64_t targets)
    {
        EXPECT_EQ(head.uid, uid);
        EXPECT_EQ(head.accessType, accessType);
        EXPECT_EQ(head.appType, appType);
        EXPECT_FALSE(head.sessionFlags.isNeedAck());
        ASSERT_EQ(head.gtidList.size(), gtidList.size());
        for (size_t i = 0; i < gtidList.size(); ++i)
        {
            EXPECT_EQ(head.gtidList.at(i), gtidList.at(i));
        }
        EXPECT_EQ(head.targets, targets);
    }
};

TEST_F(TestSessionData, CheckHandleAiChatBusinessReq_ForwardToRouter)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "test";
    sendToMe(std::move(req));

    checkOutput<AiChatBusinessReq>(router_,
                                   [&](AiChatBusinessReq &msg)
                                   {
                                       checkHeadFields(msg.head, kDefaultUid, kDefaultAccessType,
                                                       kDefaultAppType, {kDefaultGtid}, 0);
                                       EXPECT_EQ(msg.content, "test");
                                   });
}

TEST_F(TestSessionData, CheckHandleAiChatBusinessReq_RouterNotSet)
{
    restartWithoutRouter();

    EXPECT_LOG(LogLevel::DBG, 1);
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = AiChatBusinessReq{};
    fillDefaultHead(req);
    req.content = "should_drop";
    sendToMe(std::move(req));
}

TEST_F(TestSessionData, CheckHandleAiChatBusinessResp_ForwardToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 4);

    constexpr uint64_t kAccessBits = 0x1A;
    setAccessBit(kDefaultUid, kAccessBits);

    auto resp = AiChatBusinessResp{};
    fillDefaultHead(resp);
    resp.head.gtidList.push_back(kDefaultGtid + 1);
    resp.content = "response";
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(
        accessGateway_,
        [&](AiChatBusinessResp &msg)
        {
            checkHeadFields(msg.head, kDefaultUid, kDefaultAccessType, kDefaultAppType,
                            {kDefaultGtid, kDefaultGtid + 1}, kAccessBits);
            EXPECT_FALSE(msg.success);
            EXPECT_EQ(msg.content, "response");
        });
}

TEST_F(TestSessionData, CheckHandleAiChatMsgAck_ForwardToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 3);

    constexpr uint64_t kAccessBits = 0x05;
    setAccessBit(kDefaultUid, kAccessBits);

    auto ack = AiChatMsgAck{};
    fillDefaultHead(ack);
    ack.seq = kSeqMsgAck;
    ack.content = "ack";
    sendToMe(std::move(ack));

    checkOutput<AiChatMsgAck>(accessGateway_,
                              [&](AiChatMsgAck &msg)
                              {
                                  checkHeadFields(msg.head, kDefaultUid, kDefaultAccessType,
                                                  kDefaultAppType, {kDefaultGtid}, kAccessBits);
                                  EXPECT_EQ(msg.seq, kSeqMsgAck);
                                  EXPECT_EQ(msg.content, "ack");
                              });
}

TEST_F(TestSessionData, CheckHandleUserLoginSessionReq_ReplyToSender)
{
    EXPECT_LOG(LogLevel::DBG, 1);

    auto req = UserLoginSessionReq{};
    fillDefaultHead(req);
    req.head.accessType = static_cast<common::AccessType>(3);
    req.head.appType = static_cast<common::AppType>(1);
    sendToMe(std::move(req));

    checkOutput<UserLoginSessionResp>(
        [&](UserLoginSessionResp &msg)
        {
            checkHeadFields(msg.head, kDefaultUid, static_cast<common::AccessType>(3),
                            static_cast<common::AppType>(1), {kDefaultGtid}, 0);
            EXPECT_FALSE(msg.needWaitForData);
        });
}

TEST_F(TestSessionData, CheckHandleUserRegisterSessionReq_ClearsAccessBits)
{
    EXPECT_LOG(LogLevel::DBG, 4);

    setAccessBit(kDefaultUid, (1ULL << 3) | (1ULL << 5));

    auto regReq = UserRegisterSessionReq{};
    regReq.userId = 1;
    sendToMe(std::move(regReq));

    auto logoutReq = UserLogoutSessionReq{};
    fillDefaultHead(logoutReq);
    logoutReq.head.accessType = static_cast<common::AccessType>(3);
    sendToMe(std::move(logoutReq));

    checkOutput<UserLogoutSessionResp>(
        [&](UserLogoutSessionResp &msg)
        {
            checkHeadFields(msg.head, kDefaultUid, static_cast<common::AccessType>(3),
                            kDefaultAppType, {kDefaultGtid}, 0);
            EXPECT_EQ(msg.activeAdapterCount, 0);
        });
}

TEST_F(TestSessionData, CheckHandleTaskDeleteSessionReq_ForwardTaskSyncToAccessGateway)
{
    EXPECT_LOG(LogLevel::DBG, 3);

    constexpr uint64_t kAccessBits = 0x0C;
    setAccessBit(kDefaultUid, kAccessBits);

    auto req = TaskDeleteSessionReq{};
    fillDefaultHead(req);
    sendToMe(std::move(req));

    checkOutput<TaskSync>(accessGateway_,
                          [&](TaskSync &msg)
                          {
                              checkHeadFields(msg.head, kDefaultUid, kDefaultAccessType,
                                              kDefaultAppType, {kDefaultGtid}, kAccessBits);
                              EXPECT_EQ(msg.type, common::TaskSyncType::TaskDeleted);
                              EXPECT_EQ(msg.gtid, kDefaultGtid);
                          });
}

TEST_F(TestSessionData, CheckHandleTaskDeleteSessionReq_EmptyGtidList)
{
    EXPECT_LOG(LogLevel::ERR, 1);

    auto req = TaskDeleteSessionReq{};
    fillDefaultHead(req);
    req.head.gtidList.clear();
    sendToMe(std::move(req));
}

TEST_F(TestSessionData, CheckHandleUserLogoutSessionReq_ReplyToSender)
{
    EXPECT_LOG(LogLevel::DBG, 4);

    setAccessBit(kDefaultUid, 1ULL << 3);
    setAccessBit(kDefaultUid, 1ULL << 5);

    auto req = UserLogoutSessionReq{};
    fillDefaultHead(req);
    req.head.gtidList.clear();
    req.head.accessType = static_cast<common::AccessType>(3);
    req.head.appType = static_cast<common::AppType>(1);
    sendToMe(std::move(req));

    checkOutput<UserLogoutSessionResp>(
        [&](UserLogoutSessionResp &msg)
        {
            checkHeadFields(msg.head, kDefaultUid, static_cast<common::AccessType>(3),
                            static_cast<common::AppType>(1), {}, 0);
            EXPECT_EQ(msg.activeAdapterCount, 1);
        });

    auto resp = AiChatBusinessResp{};
    fillDefaultHead(resp);
    resp.content = "verifyBits";
    sendToMe(std::move(resp));

    checkOutput<AiChatBusinessResp>(accessGateway_,
                                    [&](AiChatBusinessResp &msg)
                                    {
                                        checkHeadFields(msg.head, kDefaultUid, kDefaultAccessType,
                                                        kDefaultAppType, {kDefaultGtid}, 1ULL << 5);
                                        EXPECT_FALSE(msg.success);
                                        EXPECT_EQ(msg.content, "verifyBits");
                                    });
}
} // namespace
