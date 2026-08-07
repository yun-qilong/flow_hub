#include "fw/EoTestBase.hpp"
#include "userAccess/CliAdapter.hpp"
#include "utils/LogTypes.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

namespace userAccess
{

using namespace common::message;
using utils::LogFeature;

class TestCliAdapter : public fw::EoTestBase
{
  protected:
    void SetUp() override
    {
        aiAdapterStub_ = makeStub();
        trackStub(aiAdapterStub_);

        cli_ = std::make_unique<CliAdapter>(system());
        cli_->setInput(&input_);
        cli_->setOutput(&output_);
        cli_->setExitCallback([this] { exitCalled_ = true; });
        cli_->setAiApiAdapterAddr(stubAddress(aiAdapterStub_));
        cli_->tempSetGatewayAddr(stubAddress(stubEo_));

        testee_ = cli_->myAddress();
    }

    void enterHaveNotGtid()
    {
        cli_->state_ = CliAdapter::State::HaveNotGtid;
        cli_->currentGtid_ = common::kInvalidGtid;
        cli_->waiting_ = false;
    }

    void enterEnteringKey(uint16_t gtid)
    {
        cli_->state_ = CliAdapter::State::EnteringKey;
        cli_->currentGtid_ = gtid;
        cli_->waiting_ = false;
    }

    void enterHasGtid(uint16_t gtid)
    {
        cli_->state_ = CliAdapter::State::HasGtid;
        cli_->currentGtid_ = gtid;
        cli_->waiting_ = false;
    }

    bool isHaveNotGtid() const
    {
        return cli_->state_ == CliAdapter::State::HaveNotGtid;
    }

    bool isEnteringKey() const
    {
        return cli_->state_ == CliAdapter::State::EnteringKey;
    }

    bool isHasGtid() const
    {
        return cli_->state_ == CliAdapter::State::HasGtid;
    }

    bool isWaiting() const
    {
        return cli_->waiting_;
    }

    void setWaiting(bool w)
    {
        cli_->waiting_ = w;
    }

    void callSendTaskDelete()
    {
        cli_->sendTaskDelete();
    }

    uint16_t currentGtid() const
    {
        return cli_->currentGtid_;
    }

    bool feedLine(const std::string &line)
    {
        input_.str(line + "\n");
        input_.clear();
        return cli_->readFrontend();
    }

    template <typename M>
    void sendToCli(M msg)
    {
        sendToMe(std::move(msg));
        cli_->pump();
    }

    void expectOutputContains(const std::string &sub)
    {
        EXPECT_NE(output_.str().find(sub), std::string::npos) << "output=" << output_.str();
    }

    void clearOutput()
    {
        output_.str("");
        output_.clear();
    }

    std::unique_ptr<CliAdapter> cli_;
    std::istringstream input_;
    std::ostringstream output_;
    Stub aiAdapterStub_;
    bool exitCalled_ = false;
};

TEST_F(TestCliAdapter, DispatchInput_EmptyLine_NoOp)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->dispatchInput("");

    EXPECT_EQ(output_.str(), "");
}

TEST_F(TestCliAdapter, DispatchInput_HaveNotGtid_SlashCommand)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->dispatchInput("/help");

    expectOutputContains("Commands:");
}

TEST_F(TestCliAdapter, DispatchInput_EnteringKey_TextSendsApiKey)
{
    enterEnteringKey(0x1234);

    cli_->dispatchInput("sk-abc");

    checkOutput<ApiKeyUpdate>(aiAdapterStub_,
                              [](ApiKeyUpdate &msg) { EXPECT_EQ(msg.apiKey, "sk-abc"); });
    EXPECT_TRUE(isHasGtid());
}

TEST_F(TestCliAdapter, DispatchInput_HasGtid_TextSendsChat)
{
    enterHasGtid(0x1234);

    cli_->dispatchInput("hello");

    checkOutput<AiAgoraChatReq>(
        [](AiAgoraChatReq &msg)
        {
            EXPECT_EQ(msg.head.sessionTaskId, 0x1234);
            EXPECT_EQ(msg.content, "hello");
        });
    EXPECT_TRUE(isWaiting());
}

TEST_F(TestCliAdapter, DispatchInput_HasGtid_SlashCommand)
{
    enterHasGtid(0x1234);

    cli_->dispatchInput("/quit");

    checkOutput<TaskDeleteReq>([](TaskDeleteReq &msg)
                               { EXPECT_EQ(msg.head.sessionTaskId, 0x1234); });
    EXPECT_TRUE(isWaiting());
}

TEST_F(TestCliAdapter, DispatchInput_NoGtid_Text_PromptsNoActiveTask)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->dispatchInput("hello");

    expectOutputContains("No active task. Use /new to create one.");
}

TEST_F(TestCliAdapter, DispatchInput_EnteringKey_SlashNotApiKey)
{
    enterEnteringKey(0x1234);
    clearOutput();

    cli_->dispatchInput("/help");

    expectOutputContains("Commands:");
}

TEST_F(TestCliAdapter, HandleCommand_HaveNotGtid_New)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->dispatchInput("/new");

    checkOutput<TaskCreateReq>(
        [](TaskCreateReq &msg)
        {
            EXPECT_EQ(msg.head.sessionTaskId, common::kInvalidGtid);
            EXPECT_EQ(msg.taskType, common::TaskType::AiAgora);
        });
    EXPECT_TRUE(isWaiting());
    expectOutputContains("Creating new task...");
}

TEST_F(TestCliAdapter, HandleCommand_HaveNotGtid_Exit)
{
    enterHaveNotGtid();

    cli_->dispatchInput("/exit");

    EXPECT_TRUE(exitCalled_);
}

TEST_F(TestCliAdapter, HandleCommand_HaveNotGtid_Unknown)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->dispatchInput("/foo");

    expectOutputContains("Unknown command. Use /help.");
}

TEST_F(TestCliAdapter, HandleCommand_EnteringKey_Quit)
{
    enterEnteringKey(0x1234);

    cli_->dispatchInput("/quit");

    checkOutput<TaskDeleteReq>([](TaskDeleteReq &msg)
                               { EXPECT_EQ(msg.head.sessionTaskId, 0x1234); });
    EXPECT_TRUE(isWaiting());
}

TEST_F(TestCliAdapter, HandleCommand_EnteringKey_Exit)
{
    enterEnteringKey(0x1234);

    cli_->dispatchInput("/exit");

    EXPECT_TRUE(exitCalled_);
}

TEST_F(TestCliAdapter, HandleCommand_HasGtid_Quit)
{
    enterHasGtid(0x1234);

    cli_->dispatchInput("/quit");

    checkOutput<TaskDeleteReq>([](TaskDeleteReq &msg)
                               { EXPECT_EQ(msg.head.sessionTaskId, 0x1234); });
    EXPECT_TRUE(isWaiting());
}

TEST_F(TestCliAdapter, HandleCommand_HasGtid_Exit)
{
    enterHasGtid(0x1234);

    cli_->dispatchInput("/exit");

    EXPECT_TRUE(exitCalled_);
}

TEST_F(TestCliAdapter, SendTaskDelete_Waiting_Prompts)
{
    enterHasGtid(0x1234);
    setWaiting(true);
    clearOutput();

    cli_->dispatchInput("/quit");

    expectOutputContains("Please wait for the current request.");
}

TEST_F(TestCliAdapter, SendTaskDelete_NoGtid_Prompts)
{
    enterHaveNotGtid();
    clearOutput();

    callSendTaskDelete();

    expectOutputContains("No active task.");
}

TEST_F(TestCliAdapter, SendChatMessage_Waiting_Prompts)
{
    enterHasGtid(0x1234);
    setWaiting(true);
    clearOutput();

    cli_->dispatchInput("hi");

    expectOutputContains("Please wait for the current request.");
}

TEST_F(TestCliAdapter, SendApiKey_Waiting_Prompts)
{
    enterEnteringKey(0x1234);
    setWaiting(true);
    clearOutput();

    cli_->dispatchInput("sk-abc");

    expectOutputContains("Please wait for the current request.");
}

TEST_F(TestCliAdapter, SendApiKey_NoAdapter_Resets)
{
    enterEnteringKey(0x1234);
    cli_->setAiApiAdapterAddr(fw::EoAddress{});
    clearOutput();

    cli_->dispatchInput("sk-abc");

    expectOutputContains("AI adapter not configured.");
    EXPECT_TRUE(isHaveNotGtid());
    EXPECT_EQ(currentGtid(), common::kInvalidGtid);
}

TEST_F(TestCliAdapter, SendApiKey_Success)
{
    enterEnteringKey(0x1234);
    clearOutput();

    cli_->dispatchInput("sk-abc");

    checkOutput<ApiKeyUpdate>(aiAdapterStub_,
                              [](ApiKeyUpdate &msg) { EXPECT_EQ(msg.apiKey, "sk-abc"); });
    EXPECT_TRUE(isHasGtid());
    expectOutputContains("API key set.");
}

TEST_F(TestCliAdapter, HandleTempConfig_SetsGateway)
{
    enterHaveNotGtid();
    cli_->tempSetGatewayAddr(fw::EoAddress{});

    sendToCli(TempConfig{0});
    cli_->dispatchInput("/new");

    checkOutput<TaskCreateReq>([](TaskCreateReq &) {});
}

TEST_F(TestCliAdapter, HandleChatResp_PrintsContent)
{
    enterHasGtid(0x1234);
    setWaiting(true);
    clearOutput();

    auto resp = AiAgoraChatResp{};
    fillDefaultHead(resp);
    resp.responses = "reply";
    sendToCli(std::move(resp));

    EXPECT_FALSE(isWaiting());
    expectOutputContains("reply");
}

TEST_F(TestCliAdapter, HandleTaskCreateResp_Success_EntersEnteringKey)
{
    enterHaveNotGtid();
    setWaiting(true);
    clearOutput();

    auto resp = TaskCreateResp{};
    fillDefaultHead(resp);
    resp.isSuccess = true;
    resp.head.sessionTaskId = 0x1234;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isEnteringKey());
    EXPECT_EQ(currentGtid(), 0x1234);
    EXPECT_FALSE(isWaiting());
    expectOutputContains("Task created: 0x1234");
}

TEST_F(TestCliAdapter, HandleTaskCreateResp_Failure_StaysHaveNotGtid)
{
    enterHaveNotGtid();
    setWaiting(true);
    clearOutput();

    auto resp = TaskCreateResp{};
    fillDefaultHead(resp);
    resp.isSuccess = false;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isHaveNotGtid());
    EXPECT_FALSE(isWaiting());
    expectOutputContains("Task creation failed.");
}

TEST_F(TestCliAdapter, HandleTaskCreateResp_InvalidGtid_Failure)
{
    enterHaveNotGtid();
    setWaiting(true);
    clearOutput();

    auto resp = TaskCreateResp{};
    fillDefaultHead(resp);
    resp.isSuccess = true;
    resp.head.sessionTaskId = common::kInvalidGtid;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isHaveNotGtid());
    expectOutputContains("Task creation failed.");
}

TEST_F(TestCliAdapter, HandleTaskDeleteResp_NotWaiting_NoStateChange)
{
    enterHasGtid(0x1234);
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);

    auto resp = TaskDeleteResp{};
    fillDefaultHead(resp);
    resp.head.sessionTaskId = 0x1234;
    resp.isSuccess = true;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isHasGtid());
    EXPECT_EQ(currentGtid(), 0x1234);
}

TEST_F(TestCliAdapter, HandleTaskDeleteResp_Success_Resets)
{
    enterHasGtid(0x1234);
    setWaiting(true);
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);
    clearOutput();

    auto resp = TaskDeleteResp{};
    fillDefaultHead(resp);
    resp.head.sessionTaskId = 0x1234;
    resp.isSuccess = true;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isHaveNotGtid());
    EXPECT_EQ(currentGtid(), common::kInvalidGtid);
    EXPECT_FALSE(isWaiting());
    expectOutputContains("Task deleted.");
}

TEST_F(TestCliAdapter, HandleTaskDeleteResp_Failure_KeepsGtid)
{
    enterHasGtid(0x1234);
    setWaiting(true);
    EXPECT_LOG_FEAT(LogFeature::AICHAT, 1);
    clearOutput();

    auto resp = TaskDeleteResp{};
    fillDefaultHead(resp);
    resp.head.sessionTaskId = 0x1234;
    resp.isSuccess = false;
    sendToCli(std::move(resp));

    EXPECT_TRUE(isHasGtid());
    EXPECT_EQ(currentGtid(), 0x1234);
    EXPECT_FALSE(isWaiting());
    expectOutputContains("Task deletion failed.");
}

TEST_F(TestCliAdapter, ReadLine_ValidInput)
{
    input_.str("hello\n");

    std::string line;
    EXPECT_TRUE(cli_->readLine(line));
    EXPECT_EQ(line, "hello");
}

TEST_F(TestCliAdapter, ReadLine_EmptyLine)
{
    input_.str("\n");

    std::string line;
    EXPECT_FALSE(cli_->readLine(line));
}

TEST_F(TestCliAdapter, ReadLine_EOF)
{
    std::string line;
    EXPECT_FALSE(cli_->readLine(line));
}

TEST_F(TestCliAdapter, ReadFrontend_Waiting_DropsInput)
{
    enterHasGtid(0x1234);
    setWaiting(true);

    EXPECT_FALSE(feedLine("hi"));
}

TEST_F(TestCliAdapter, ReadFrontend_DispatchesCommand)
{
    enterHaveNotGtid();

    EXPECT_TRUE(feedLine("/new"));

    checkOutput<TaskCreateReq>([](TaskCreateReq &) {});
}

TEST_F(TestCliAdapter, ShowPrompt_Waiting)
{
    setWaiting(true);
    clearOutput();

    cli_->showPrompt();

    expectOutputContains("... ");
}

TEST_F(TestCliAdapter, ShowPrompt_EnteringKey)
{
    enterEnteringKey(0x1234);
    clearOutput();

    cli_->showPrompt();

    expectOutputContains("Enter API key: ");
}

TEST_F(TestCliAdapter, ShowPrompt_HasGtid)
{
    enterHasGtid(0x1234);
    clearOutput();

    cli_->showPrompt();

    expectOutputContains("[0x1234]> ");
}

TEST_F(TestCliAdapter, ShowPrompt_Default)
{
    enterHaveNotGtid();
    clearOutput();

    cli_->showPrompt();

    expectOutputContains("> ");
}

} // namespace userAccess
