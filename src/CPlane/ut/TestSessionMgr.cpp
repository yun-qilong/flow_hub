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

    // 递增轮转分配（RI0003）：刚回收的 GTID 不立即复用，分配下一个空闲槽
    auto second = createTask();
    EXPECT_NE(second, first);
    EXPECT_NE(second, common::kInvalidGtid);
}

} // namespace
