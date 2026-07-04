include type/types.mt
include common/SessionFlags.hpp

struct UserHead
    uint16 uid
    vector<GTID> gtidList
    AccessType accessType
    AppType appType
    SessionFlags sessionFlags
    uint64 targets
