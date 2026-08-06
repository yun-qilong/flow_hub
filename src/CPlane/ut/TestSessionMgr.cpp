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
        businessMgr_ = makeStub();
        trackStub(accessGateway_);
        trackStub(businessMgr_);

        testee_ = spawn<CPlane::SessionMgr>(pool_, stubAddress(accessGateway_));

        checkOutput<TempConfig>(accessGateway_, [](TempConfig &msg) { EXPECT_EQ(msg.tag, 1); });
        sendToMeFrom(businessMgr_, testee_, TempConfig{4});
    }

    void TearDown() override
    {
        utils::gSysLog() = nullptr;
    }

    common::TaskPool pool_{};
    Stub accessGateway_;
    Stub businessMgr_;
    std::unique_ptr<utils::MockSysLog> mockLog_;

    common::GTID createTask()
    {
        auto req = TaskCreateReq{};
        fillDefaultHead(req);
        req.taskType = common::TaskType::AiAgora;
        sendToMe(std::move(req));

        common::GTID created = common::kInvalidGtid;
        checkOutput<TaskCreateResp>(accessGateway_,
                                    [&](TaskCreateResp &msg)
                                    {
                                        EXPECT_TRUE(msg.isSuccess);
                                        created = msg.head.sessionTaskId;
                                    });
        return created;
    }

    void deleteTask(common::GTID gtid)
    {
        auto del = TaskDeleteReq{};
        fillDefaultHead(del);
        del.head.sessionTaskId = gtid;
        sendToMe(std::move(del));

        checkOutput<TaskDeleteResp>(accessGateway_,
                                    [&](TaskDeleteResp &msg) { EXPECT_TRUE(msg.isSuccess); });
    }
};

TEST_F(TestSessionMgr, CheckHandleTaskCreateReq_Success)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(2);

    auto created = createTask();
    EXPECT_NE(created, common::kInvalidGtid);
}

TEST_F(TestSessionMgr, CheckHandleTaskDeleteReq_Success)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(4);

    auto created = createTask();
    deleteTask(created);
}

TEST_F(TestSessionMgr, CheckHandleTaskDeleteReq_InvalidGtid)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(1);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::ERR, ::testing::_)).Times(1);

    auto del = TaskDeleteReq{};
    fillDefaultHead(del);
    del.head.sessionTaskId = common::kInvalidGtid;
    sendToMe(std::move(del));

    checkOutput<TaskDeleteResp>(accessGateway_,
                                [&](TaskDeleteResp &msg) { EXPECT_FALSE(msg.isSuccess); });
}

TEST_F(TestSessionMgr, CheckHandleTaskDeleteReq_AfterDeleteAllocatesNext)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(6);

    auto first = createTask();
    deleteTask(first);

    auto second = createTask();
    EXPECT_NE(second, first);
    EXPECT_NE(second, common::kInvalidGtid);
}

TEST_F(TestSessionMgr, CheckHandleBusTaskCreateReq_Success)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(2);

    BusTaskCreateReq req;
    req.head.sessionTaskId = 0x7A01;
    req.head.busTaskIds = {};
    req.taskTypes = {common::TaskType::AiChat, common::TaskType::AiChat};
    sendToMe(std::move(req));

    checkOutput<BusTaskCreateResp>(
        [](BusTaskCreateResp &msg)
        {
            ASSERT_TRUE(msg.isSuccess);
            ASSERT_EQ(msg.head.busTaskIds.size(), 2u);
            for (auto gtid : msg.head.busTaskIds)
            {
                EXPECT_EQ(static_cast<common::TaskType>(gtid >> 6), common::TaskType::AiChat);
            }
        });
}

TEST_F(TestSessionMgr, CheckHandleBusTaskCreateReq_FullRollsBack)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(5);
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::WRN, ::testing::_)).Times(1);

    BusTaskCreateReq fill;
    fill.head.sessionTaskId = 0x7A01;
    fill.head.busTaskIds = {};
    fill.taskTypes.assign(63, common::TaskType::AiChat);
    sendToMe(std::move(fill));
    checkOutput<BusTaskCreateResp>(
        [](BusTaskCreateResp &msg)
        {
            ASSERT_TRUE(msg.isSuccess);
            ASSERT_EQ(msg.head.busTaskIds.size(), 63u);
        });

    BusTaskCreateReq over;
    over.head.sessionTaskId = 0x7A01;
    over.head.busTaskIds = {};
    over.taskTypes.assign(2, common::TaskType::AiChat);
    sendToMe(std::move(over));
    checkOutput<BusTaskCreateResp>(
        [](BusTaskCreateResp &msg)
        {
            EXPECT_FALSE(msg.isSuccess);
            EXPECT_TRUE(msg.head.busTaskIds.empty());
        });

    BusTaskCreateReq again;
    again.head.sessionTaskId = 0x7A01;
    again.head.busTaskIds = {};
    again.taskTypes = {common::TaskType::AiChat};
    sendToMe(std::move(again));
    checkOutput<BusTaskCreateResp>([](BusTaskCreateResp &msg) { ASSERT_TRUE(msg.isSuccess); });
}

TEST_F(TestSessionMgr, CheckHandleBusTaskDeleteReq_Recycles)
{
    EXPECT_CALL(*mockLog_, log(utils::LogLevel::INFO, ::testing::_)).Times(6);

    BusTaskCreateReq create;
    create.head.sessionTaskId = 0x7A01;
    create.head.busTaskIds = {};
    create.taskTypes = {common::TaskType::AiChat, common::TaskType::AiChat};
    sendToMe(std::move(create));

    std::vector<common::GTID> created;
    checkOutput<BusTaskCreateResp>(
        [&](BusTaskCreateResp &msg)
        {
            ASSERT_TRUE(msg.isSuccess);
            created = msg.head.busTaskIds;
        });

    BusTaskDeleteReq del;
    del.head.sessionTaskId = 0x7A01;
    del.head.busTaskIds = created;
    sendToMe(std::move(del));
    checkOutput<BusTaskDeleteResp>([](BusTaskDeleteResp &msg) { EXPECT_TRUE(msg.isSuccess); });

    BusTaskCreateReq again;
    again.head.sessionTaskId = 0x7A01;
    again.head.busTaskIds = {};
    again.taskTypes = {common::TaskType::AiChat};
    sendToMe(std::move(again));
    checkOutput<BusTaskCreateResp>([](BusTaskCreateResp &msg) { ASSERT_TRUE(msg.isSuccess); });
}

} // namespace
