include message/MsgHead.mt

message AiChatServiceResp
    MsgHead head
    string content
    bool success
    string errorMsg