include message/MsgHead.mt

message AiChatServiceReq
    MsgHead head
    string messagesJson
    string modelName
    double temperature