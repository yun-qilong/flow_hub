include message/UserHead.mt
include message/TaskCreateCookie.mt

message TaskCreateResp
    UserHead head
    bool isSuccess
    TaskCreateCookie cookie
