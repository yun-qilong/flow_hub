include message/UserHead.mt

message AiAgoraChatResp
    UserHead head
    bool isComplete
    bool hasResponses
    uint8 endReason
    uint8 errorCode
    uint8 currentState
    string responses
