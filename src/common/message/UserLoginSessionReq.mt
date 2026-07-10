include message/UserHead.mt
include type/types.mt

message UserLoginSessionReq
    UserHead head
    StaticVector<GTID, kMaxGtidsPerUser> gtids
