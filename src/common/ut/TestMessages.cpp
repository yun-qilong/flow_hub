#include "fw/EoTestBase.hpp"
#include "fw/MessageRoundTrip.hpp"
#include "generated/message/Messages.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace common::message;

class TestMessages : public fw::EoTestBase
{
  protected:
    template <typename M>
    void assertRoundTrip(const M &src)
    {
        ASSERT_TRUE(fw::roundTripSerialize(system(), src));
    }

    void fillDefaultHead(UserHead &head)
    {
        head.sessionTaskId = 0x7A01;
        head.busTaskIds = {0x9001, 0x9002};
    }
};

TEST_F(TestMessages, RoundTrip_AiAgoraChatReq)
{
    AiAgoraChatReq msg;
    fillDefaultHead(msg.head);
    msg.content = "question";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiAgoraChatResp)
{
    AiAgoraChatResp msg;
    fillDefaultHead(msg.head);
    msg.isComplete = true;
    msg.hasResponses = true;
    msg.endReason = 1;
    msg.errorCode = 0;
    msg.currentState = 2;
    msg.responses = R"([{"aiIndex":0,"content":"a"}])";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiAgoraResetReq)
{
    AiAgoraResetReq msg;
    fillDefaultHead(msg.head);
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiAgoraResetResp)
{
    AiAgoraResetResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    msg.estimatedTopicCount = 5;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatConfigReq)
{
    AiChatConfigReq msg;
    fillDefaultHead(msg.head);
    msg.aiIndex = 0;
    msg.systemPrompt = "system prompt";
    msg.payload = R"({"apiUrl":"u","apiKey":"k","model":"m","temperature":0.7})";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatConfigResp)
{
    AiChatConfigResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatReq)
{
    AiChatReq msg;
    fillDefaultHead(msg.head);
    msg.messagesJson = R"([{"role":"user","content":"hi"}])";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatResp)
{
    AiChatResp msg;
    fillDefaultHead(msg.head);
    msg.success = true;
    msg.aiIndex = 1;
    msg.content = "answer";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatServiceReq)
{
    AiChatServiceReq msg;
    fillDefaultHead(msg.head);
    msg.messagesJson = R"([{"role":"system","content":"s"}])";
    msg.modelName = "deepseek-v4-flash";
    msg.temperature = 0.7;
    msg.reqSeq = 3;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_AiChatServiceResp)
{
    AiChatServiceResp msg;
    fillDefaultHead(msg.head);
    msg.success = true;
    msg.content = "service answer";
    msg.reqSeq = 3;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_ApiKeyUpdate)
{
    ApiKeyUpdate msg;
    msg.apiKey = "sk-test-key";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_BusTaskCreateReq)
{
    BusTaskCreateReq msg;
    fillDefaultHead(msg.head);
    msg.taskTypes = {common::TaskType::AiChat};
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_BusTaskCreateResp)
{
    BusTaskCreateResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_BusTaskDeleteReq)
{
    BusTaskDeleteReq msg;
    fillDefaultHead(msg.head);
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_BusTaskDeleteResp)
{
    BusTaskDeleteResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskConfigReq)
{
    TaskConfigReq msg;
    fillDefaultHead(msg.head);
    msg.payload = R"({"aiCount":1,"hasJudge":false,"maxRounds":1})";
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskConfigResp)
{
    TaskConfigResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    msg.estimatedTopicCount = 3;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskCreateReq)
{
    TaskCreateReq msg;
    fillDefaultHead(msg.head);
    msg.taskType = common::TaskType::AiAgora;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskCreateResp)
{
    TaskCreateResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskDeleteReq)
{
    TaskDeleteReq msg;
    fillDefaultHead(msg.head);
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TaskDeleteResp)
{
    TaskDeleteResp msg;
    fillDefaultHead(msg.head);
    msg.isSuccess = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_TempConfig)
{
    TempConfig msg;
    msg.tag = 5;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_RouterConfigReq)
{
    RouterConfigReq msg;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_RouterConfigResp)
{
    RouterConfigResp msg;
    msg.success = true;
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_RouterReconfigReq)
{
    RouterReconfigReq msg;
    RouteEntry entry;
    entry.taskType = common::TaskType::AiChat;
    msg.entries = {entry};
    assertRoundTrip(msg);
}

TEST_F(TestMessages, RoundTrip_RouterReconfigResp)
{
    RouterReconfigResp msg;
    msg.success = true;
    assertRoundTrip(msg);
}

} // namespace
