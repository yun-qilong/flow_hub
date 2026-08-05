include type/types.mt

struct UserHead
    uint16 uid
    vector<GTID> gtidList
    AccessType accessType
    AppType appType
    uint64 targets
