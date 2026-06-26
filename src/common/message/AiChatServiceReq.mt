include message/UserHead.mt

message AiChatServiceReq
    UserHead head
    string messagesJson
    string modelName
    double temperature
    uint16 reqSeq