context AiChatContext
    uint8[64] modelName
    uint8[128] apiUrl
    uint8[128] apiKey
    double temperature
    AiIndex aiIndex = 0xFF
    uint8[4096] systemPrompt
