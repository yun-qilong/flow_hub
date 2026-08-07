# Message Structure Definition

---

## UserHead


| Field | Type | Range |
|------|------|--------|
| `sessionTaskId` | `GTID` | `[0x7000, 0x7FFF]`, `kInvalidGtid`(0x0000) |
| `busTaskIds` | `GTID[]` | len `≥ 0` |



---

## TaskCreateReq



| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `taskType` | `TaskType` | — |
| `cookie` | `TaskCreateCookie` | — |

---

## TaskCreateResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |
| `cookie` | `TaskCreateCookie` | — |

---

## TaskDeleteReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |

---

## TaskDeleteResp

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |

---

## BusTaskCreateReq

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `taskTypes` | `TaskType[]` | len `≥ 1` |

---

## BusTaskCreateResp

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |

---

## BusTaskDeleteReq

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |

---

## BusTaskDeleteResp

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |

---

## TaskCreateCookie

| Field | Type | Range |
|------|------|--------|
| `adapterAddr` | `EoAddress` | — |

---

## TaskConfigReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `payload` | `string` | — |

`payload` JSON fields:

| Field | Type | Range |
|------|------|--------|
| `aiCount` | int | 1~8 |
| `hasJudge` | bool | `true` / `false` |
| `maxRounds` | int | ≥ 1 |
| `maxResponseLength` | int | > 0 |
| `timeoutMs` | int | > 0 |
| `configs` | `TaskCfg[]` | len = `aiCount + (hasJudge ? 1 : 0)` |

`TaskCfg` fields:

| Field | Type | Range |
|------|------|--------|
| `apiUrl` | string | — |
| `apiKey` | string | — |
| `model` | string | — |
| `systemPrompt` | string | — |
| `temperature` | float | 0.0~2.0 |

---

## TaskConfigResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |
| `estimatedTopicCount` | `uint16_t` | ≥ 0 |

---

## AiChatConfigReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `aiIndex` | `AiIndex` | 0~7 / `kJudgeIndex` |
| `systemPrompt` | `string` | — |
| `payload` | `string` | — |

`payload` JSON fields:

| Field | Type | Range |
|------|------|--------|
| `apiUrl` | string | — |
| `apiKey` | string | — |
| `model` | string | — |
| `temperature` | float | 0.0~2.0 |

---

## AiChatConfigResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |
| `aiIndex` | `AiIndex` | 0~7 / `kJudgeIndex` |

---

## AiAgoraChatReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `content` | `string` | — |


---

## AiAgoraChatResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isComplete` | `bool` | `true` / `false` |
| `hasResponses` | `bool` | `true` / `false` |
| `endReason` | `uint8_t` | `[0, 3]` |
| `errorCode` | `uint8_t` | `[0, 5]` |
| `currentState` | `uint8_t` | `[0, 5]` |
| `responses` | `string` | — |


---

## AiChatReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `messagesJson` | `string` | — |


---

## AiChatResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `success` | `bool` | `true` / `false` |
| `aiIndex` | `AiIndex` | 0~7 / `kJudgeIndex` |
| `content` | `string` | — |

---

## AiChatServiceReq

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `apiUrl` | `string` | — |
| `apiKey` | `string` | — |
| `modelName` | `string` | — |
| `temperature` | `double` | `[0.0, 2.0]` |
| `messagesJson` | `string` | — |

---

## AiChatServiceResp

| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `success` | `bool` | `true` / `false` |
| `content` | `string` | — |

---

## AiAgoraResetReq


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |


---

## AiAgoraResetResp


| Field | Type | Range |
|------|------|--------|
| `head` | `UserHead` | — |
| `isSuccess` | `bool` | `true` / `false` |
| `estimatedTopicCount` | `uint16_t` | ≥ 0 |
