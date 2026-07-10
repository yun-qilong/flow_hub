include message/UserHead.mt
include type/types.mt

message UserLoginResp
    UserHead head
    string username
    uint8 connectionId
    bool success
    bool needWaitForData
    StaticVector<GTID, kMaxGtidsPerUser> gtids
