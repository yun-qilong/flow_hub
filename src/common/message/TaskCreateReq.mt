include message/UserHead.mt
include message/TaskCreateCookie.mt

message TaskCreateReq
    UserHead head
    TaskType taskType
    TaskCreateCookie cookie
