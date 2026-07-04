include message/UserHead.mt

message UserLoginResp
    UserHead head
    string username
    uint8 connectionId
    bool success
    bool needWaitForData
    StaticVector<uint16, kMaxGtidsPerUser> gtids
