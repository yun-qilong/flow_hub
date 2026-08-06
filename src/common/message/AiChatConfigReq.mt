include message/UserHead.mt

message AiChatConfigReq
    UserHead head
    AiIndex aiIndex
    string systemPrompt
    string payload
