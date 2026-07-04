include message/UserHead.mt

message UserRegisterResp
    UserHead head
    string username
    uint8 connectionId
    bool success
