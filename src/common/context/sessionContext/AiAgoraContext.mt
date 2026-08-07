context AiAgoraContext
    uint16[8] debateTaskIds = 0xFFFF
    uint16 judgeTaskId = 0xFFFF
    uint8 maxRounds
    uint16 maxResponseLength
    uint32 maxCharPerTopic
    uint32 timeoutMs
    uint16 pendingReplies
    uint8[10485760] topicBaseJson
    uint8[32768] lastRoundResponses
    uint8 currentRound
    uint8 state
