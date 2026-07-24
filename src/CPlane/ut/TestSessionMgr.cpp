#include "common/TaskPool.hpp"
#include "CPlane/SessionMgr.hpp"
#include "fw/EoTestBase.hpp"
#include "utils/SysLog.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

class TestSessionMgr : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        mockLog_ = std::make_unique<utils::MockSysLog>();
        utils::gSysLog() = mockLog_.get();

        accessGateway_ = makeStub();
        sessionData_ = makeStub();
        businessMgr_ = makeStub();
        trackStub(accessGateway_);
        trackStub(sessionData_);
        trackStub(businessMgr_);

        testee_ = spawn<CPlane::SessionMgr>(pool_, stubAddress(accessGateway_),
                                            stubAddress(sessionData_));

        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 1); });
        sendToMeFrom(businessMgr_, testee_, TempConfig{4});
    }

    void TearDown() override
    {
        utils::gSysLog() = nullptr;
    }

    common::TaskPool pool_{};
    Stub accessGateway_;
    Stub sessionData_;
    Stub businessMgr_;
    std::unique_ptr<utils::MockSysLog> mockLog_;

    static constexpr common::AccessType kReqAccessType = kDefaultAccessType;
    static constexpr common::AccessType kSecondAccessType = static_cast<common::AccessType>(3);
    static constexpr common::AppType kReqAppType = kDefaultAppType;
    static constexpr common::AppType kReqAppTypeAiAgora = static_cast<common::AppType>(0);
    static constexpr common::AppType kReqAppTypeAiDiscussion = static_cast<common::AppType>(1);

    static constexpr uint8_t kTestConnId = 42;

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

TEST_F(TestSessionMgr, CheckHandleUserRegisterReq_UsernameTooLong)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(1);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::ERR, ::testing::_)).Times(1);

    auto req = UserRegisterReq{};
    fillDefaultHead(req);
    req.head.uid = common::kInvalidUid;
    req.head.gtidList.clear();
    req.username = std::string(13, 'x');
    req.connectionId = kTestConnId;
    sendToMe(std::move(req));

    checkOutput<UserRegisterResp>(accessGateway_,
                                  [&](UserRegisterResp &msg)
                                  {
                                      checkHeadFields(msg.head, common::kInvalidUid, kReqAccessType,
                                                      kReqAppType, {}, 0);
                                      EXPECT_EQ(msg.username, std::string(13, 'x'));
                                      EXPECT_EQ(msg.connectionId, kTestConnId);
                                      EXPECT_EQ(msg.success, false);
                                  });
}

TEST_F(TestSessionMgr, CheckHandleUserRegisterReq_DuplicateUsername)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(3);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::ERR, ::testing::_)).Times(1);

    auto first = UserRegisterReq{};
    fillDefaultHead(first);
    first.head.uid = common::kInvalidUid;
    first.head.gtidList.clear();
    first.username = "dup";
    first.connectionId = kTestConnId;
    sendToMe(std::move(first));
    checkOutput<UserRegisterResp>(accessGateway_, [](UserRegisterResp &) {});
    checkOutput<UserRegisterSessionReq>(sessionData_, [](UserRegisterSessionReq &) {});

    auto second = UserRegisterReq{};
    fillDefaultHead(second);
    second.head.uid = common::kInvalidUid;
    second.head.gtidList.clear();
    second.username = "dup";
    second.connectionId = kTestConnId;
    sendToMe(std::move(second));

    checkOutput<UserRegisterResp>(accessGateway_,
                                  [&](UserRegisterResp &msg)
                                  {
                                      checkHeadFields(msg.head, common::kInvalidUid, kReqAccessType,
                                                      kReqAppType, {}, 0);
                                      EXPECT_EQ(msg.username, "dup");
                                      EXPECT_EQ(msg.connectionId, kTestConnId);
                                      EXPECT_EQ(msg.success, false);
                                  });
}

TEST_F(TestSessionMgr, CheckHandleUserLoginReq_UserNotFound)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(1);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::ERR, ::testing::_)).Times(1);

    auto req = UserLoginReq{};
    fillDefaultHead(req);
    req.head.uid = common::kInvalidUid;
    req.head.gtidList.clear();
    req.username = "nobody";
    req.connectionId = kTestConnId;
    sendToMe(std::move(req));

    checkOutput<UserLoginResp>(accessGateway_,
                               [&](UserLoginResp &msg)
                               {
                                   checkHeadFields(msg.head, common::kInvalidUid, kReqAccessType,
                                                   kReqAppType, {}, 0);
                                   EXPECT_EQ(msg.username, "nobody");
                                   EXPECT_EQ(msg.connectionId, kTestConnId);
                                   EXPECT_EQ(msg.success, false);
                                   EXPECT_EQ(msg.needWaitForData, false);
                                   EXPECT_TRUE(msg.gtids.empty());
                               });
}

TEST_F(TestSessionMgr, CheckHandleSessionLifecycle)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(10);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::DBG, ::testing::_)).Times(20);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::ERR, ::testing::_)).Times(2);

    constexpr uint16_t kUserUid = 1;

    // ===== register =====
    auto reg = UserRegisterReq{};
    fillDefaultHead(reg);
    reg.head.uid = common::kInvalidUid;
    reg.head.gtidList.clear();
    reg.head.appType = kReqAppTypeAiDiscussion;
    reg.username = "lifeuser";
    reg.connectionId = kTestConnId;
    sendToMe(std::move(reg));
    checkOutput<UserRegisterResp>(accessGateway_,
                                  [&](UserRegisterResp &msg)
                                  {
                                      checkHeadFields(msg.head, common::kInvalidUid, kReqAccessType,
                                                      kReqAppTypeAiDiscussion, {}, 0);
                                      EXPECT_EQ(msg.username, "lifeuser");
                                      EXPECT_EQ(msg.connectionId, kTestConnId);
                                      EXPECT_EQ(msg.success, true);
                                  });
    checkOutput<UserRegisterSessionReq>(sessionData_,
                                        [&](UserRegisterSessionReq &msg)
                                        {
                                            checkHeadFields(msg.head, common::kInvalidUid,
                                                            kReqAccessType, kReqAppTypeAiDiscussion,
                                                            {}, 0);
                                            EXPECT_EQ(msg.userId, 0);
                                        });

    // ===== login frontend1 (accessType=7) =====
    {
        auto req = UserLoginReq{};
        fillDefaultHead(req);
        req.head.uid = common::kInvalidUid;
        req.head.gtidList.clear();
        req.head.appType = kReqAppTypeAiDiscussion;
        req.username = "lifeuser";
        req.connectionId = kTestConnId;
        sendToMe(std::move(req));
        checkOutputAndReply<UserLoginSessionReq, UserLoginSessionResp>(
            sessionData_,
            [&](UserLoginSessionReq &msg)
            {
                checkHeadFields(msg.head, kUserUid, kReqAccessType, kReqAppTypeAiDiscussion, {}, 0);
                EXPECT_TRUE(msg.gtids.empty());
            },
            [&]()
            {
                UserLoginSessionResp resp;
                resp.head.uid = kUserUid;
                resp.needWaitForData = false;
                return resp;
            }());
        checkOutput<UserLoginResp>(accessGateway_,
                                   [&](UserLoginResp &msg)
                                   {
                                       checkHeadFields(msg.head, kUserUid, kReqAccessType,
                                                       kReqAppTypeAiDiscussion, {}, 0);
                                       EXPECT_EQ(msg.username, "lifeuser");
                                       EXPECT_EQ(msg.connectionId, kTestConnId);
                                       EXPECT_EQ(msg.success, true);
                                       EXPECT_EQ(msg.needWaitForData, false);
                                       EXPECT_TRUE(msg.gtids.empty());
                                   });
    }

    // ===== create Task1, Task2, Task3 via frontend1 =====
    common::GTID g1 = common::kInvalidGtid;
    common::GTID g2 = common::kInvalidGtid;
    common::GTID g3 = common::kInvalidGtid;
    {
        auto c1 = TaskCreateReq{};
        fillDefaultHead(c1);
        c1.head.uid = kUserUid;
        c1.head.appType = kReqAppTypeAiDiscussion;
        c1.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(c1));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        g1 = msg.head.gtidList[0];
                                        EXPECT_NE(g1, 0);
                                        checkHeadFields(msg.head, kUserUid, kReqAccessType,
                                                        kReqAppTypeAiDiscussion, {g1}, 0);
                                        EXPECT_EQ(msg.success, true);
                                    });
    }
    {
        auto c2 = TaskCreateReq{};
        fillDefaultHead(c2);
        c2.head.uid = kUserUid;
        c2.head.appType = kReqAppTypeAiDiscussion;
        c2.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(c2));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        g2 = msg.head.gtidList[0];
                                        EXPECT_NE(g2, 0);
                                    });
    }
    {
        auto c3 = TaskCreateReq{};
        fillDefaultHead(c3);
        c3.head.uid = kUserUid;
        c3.head.appType = kReqAppTypeAiDiscussion;
        c3.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(c3));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        g3 = msg.head.gtidList[0];
                                        EXPECT_NE(g3, 0);
                                    });
    }

    // ===== delete Task2 via frontend1 =====
    {
        auto d2 = TaskDeleteReq{};
        fillDefaultHead(d2);
        d2.head.uid = kUserUid;
        d2.head.gtidList = {g2};
        sendToMe(std::move(d2));
        checkOutput<TaskDeleteSessionReq>(
            sessionData_, [&](TaskDeleteSessionReq &msg)
            { checkHeadFields(msg.head, kUserUid, kReqAccessType, kReqAppType, {g2}, 0); });
        checkOutput<TaskDeleteResp>(accessGateway_,
                                    [&](TaskDeleteResp &msg)
                                    {
                                        checkHeadFields(msg.head, kUserUid, kReqAccessType,
                                                        kReqAppType, {g2}, 0);
                                        EXPECT_EQ(msg.success, true);
                                    });
    }

    // ===== login frontend2 (accessType=3) — gtids should be {g1, g3} =====
    {
        auto req = UserLoginReq{};
        fillDefaultHead(req);
        req.head.uid = common::kInvalidUid;
        req.head.gtidList.clear();
        req.head.accessType = kSecondAccessType;
        req.head.appType = kReqAppTypeAiDiscussion;
        req.username = "lifeuser";
        req.connectionId = kTestConnId;
        sendToMe(std::move(req));
        checkOutputAndReply<UserLoginSessionReq, UserLoginSessionResp>(
            sessionData_,
            [&](UserLoginSessionReq &msg)
            {
                checkHeadFields(msg.head, kUserUid, kSecondAccessType, kReqAppTypeAiDiscussion, {},
                                0);
                ASSERT_EQ(msg.gtids.size(), 2);
                msg.gtids.at(0).useOrFailed([&](auto &v) { EXPECT_EQ(v, g1); }, []() { FAIL(); });
                msg.gtids.at(1).useOrFailed([&](auto &v) { EXPECT_EQ(v, g3); }, []() { FAIL(); });
            },
            [&]()
            {
                UserLoginSessionResp resp;
                resp.head.uid = kUserUid;
                resp.needWaitForData = true;
                return resp;
            }());
        checkOutput<UserLoginResp>(
            accessGateway_,
            [&](UserLoginResp &msg)
            {
                checkHeadFields(msg.head, kUserUid, kSecondAccessType, kReqAppTypeAiDiscussion, {},
                                0);
                EXPECT_EQ(msg.username, "lifeuser");
                EXPECT_EQ(msg.connectionId, kTestConnId);
                EXPECT_EQ(msg.success, true);
                EXPECT_EQ(msg.needWaitForData, true);
                ASSERT_EQ(msg.gtids.size(), 2);
                msg.gtids.at(0).useOrFailed([&](auto &v) { EXPECT_EQ(v, g1); }, []() { FAIL(); });
                msg.gtids.at(1).useOrFailed([&](auto &v) { EXPECT_EQ(v, g3); }, []() { FAIL(); });
            });
    }

    // ===== create Task4 via frontend2 =====
    common::GTID g4 = common::kInvalidGtid;
    {
        auto c4 = TaskCreateReq{};
        fillDefaultHead(c4);
        c4.head.uid = kUserUid;
        c4.head.accessType = kSecondAccessType;
        c4.head.appType = kReqAppTypeAiDiscussion;
        c4.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(c4));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        g4 = msg.head.gtidList[0];
                                        EXPECT_NE(g4, 0);
                                    });
    }

    // ===== logout frontend1 (accessType=7) =====
    {
        auto lo = UserLogoutReq{};
        fillDefaultHead(lo);
        lo.head.uid = kUserUid;
        lo.head.gtidList.clear();
        lo.head.accessType = kReqAccessType;
        sendToMe(std::move(lo));
        checkOutputAndReply<UserLogoutSessionReq, UserLogoutSessionResp>(
            sessionData_,
            [&](UserLogoutSessionReq &msg)
            { checkHeadFields(msg.head, kUserUid, kReqAccessType, kReqAppType, {}, 0); },
            [&]()
            {
                UserLogoutSessionResp resp;
                resp.head.uid = kUserUid;
                resp.activeAdapterCount = 1;
                return resp;
            }());
        checkOutput<UserLogoutResp>(accessGateway_,
                                    [&](UserLogoutResp &msg)
                                    {
                                        checkHeadFields(msg.head, kUserUid, kReqAccessType,
                                                        kReqAppType, {}, 0);
                                        EXPECT_EQ(msg.success, true);
                                    });
    }

    // ===== create Task5 via frontend2 =====
    common::GTID g5 = common::kInvalidGtid;
    {
        auto c5 = TaskCreateReq{};
        fillDefaultHead(c5);
        c5.head.uid = kUserUid;
        c5.head.accessType = kSecondAccessType;
        c5.head.appType = kReqAppTypeAiDiscussion;
        c5.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(c5));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        ASSERT_EQ(msg.head.gtidList.size(), 1);
                                        g5 = msg.head.gtidList[0];
                                        EXPECT_NE(g5, 0);
                                    });
    }

    // ===== delete Task4 via frontend2 =====
    {
        auto d4 = TaskDeleteReq{};
        fillDefaultHead(d4);
        d4.head.uid = kUserUid;
        d4.head.accessType = kSecondAccessType;
        d4.head.gtidList = {g4};
        sendToMe(std::move(d4));
        checkOutput<TaskDeleteSessionReq>(sessionData_, [](TaskDeleteSessionReq &) {});
        checkOutput<TaskDeleteResp>(accessGateway_,
                                    [](TaskDeleteResp &msg) { EXPECT_EQ(msg.success, true); });
    }

    // ===== delete Task1 via frontend2 =====
    {
        auto d1 = TaskDeleteReq{};
        fillDefaultHead(d1);
        d1.head.uid = kUserUid;
        d1.head.accessType = kSecondAccessType;
        d1.head.gtidList = {g1};
        sendToMe(std::move(d1));
        checkOutput<TaskDeleteSessionReq>(sessionData_, [](TaskDeleteSessionReq &) {});
        checkOutput<TaskDeleteResp>(accessGateway_,
                                    [](TaskDeleteResp &msg) { EXPECT_EQ(msg.success, true); });
    }

    // ===== create Task with invalid AppType via frontend2 =====
    {
        auto bad = TaskCreateReq{};
        fillDefaultHead(bad);
        bad.head.uid = 2;
        bad.head.accessType = kSecondAccessType;
        bad.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(bad));
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        checkHeadFields(msg.head, 2, kSecondAccessType, kReqAppType,
                                                        {kDefaultGtid}, 0);
                                        EXPECT_EQ(msg.success, false);
                                    });
    }

    // ===== delete Task1 again (gtid not found) via frontend2 =====
    {
        auto d1b = TaskDeleteReq{};
        fillDefaultHead(d1b);
        d1b.head.uid = kUserUid;
        d1b.head.accessType = kSecondAccessType;
        d1b.head.gtidList = {g1};
        sendToMe(std::move(d1b));
        checkOutput<TaskDeleteResp>(accessGateway_,
                                    [](TaskDeleteResp &msg) { EXPECT_EQ(msg.success, false); });
    }

    // ===== logout frontend2 (accessType=3) =====
    {
        auto lo = UserLogoutReq{};
        fillDefaultHead(lo);
        lo.head.uid = kUserUid;
        lo.head.gtidList.clear();
        lo.head.accessType = kSecondAccessType;
        sendToMe(std::move(lo));
        checkOutputAndReply<UserLogoutSessionReq, UserLogoutSessionResp>(
            sessionData_, [](UserLogoutSessionReq &) {},
            [&]()
            {
                UserLogoutSessionResp resp;
                resp.head.uid = kUserUid;
                resp.activeAdapterCount = 0;
                return resp;
            }());
        checkOutput<UserLogoutResp>(accessGateway_,
                                    [](UserLogoutResp &msg) { EXPECT_EQ(msg.success, true); });
    }

    // ===== delete user =====
    {
        auto del = UserDeleteReq{};
        fillDefaultHead(del);
        del.head.uid = kUserUid;
        del.head.gtidList.clear();
        sendToMe(std::move(del));
        checkOutput<UserDeleteResp>(accessGateway_,
                                    [&](UserDeleteResp &msg)
                                    {
                                        checkHeadFields(msg.head, kUserUid, kReqAccessType,
                                                        kReqAppType, {}, 0);
                                        EXPECT_EQ(msg.success, true);
                                    });
    }
}

} // namespace
