include message/UserHead.mt

message UserLoginSessionReq
    UserHead head
    StaticVector<uint16, kMaxGtidsPerUser> gtids
