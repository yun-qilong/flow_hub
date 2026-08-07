#pragma once

#include "common/Constants.hpp"
#include "fw/EoBase.hpp"
#include "generated/context/AiAgoraContext.hpp"
#include "generated/message/Messages.hpp"
#include "utils/Result.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace common
{
class TaskPool;
}

namespace DPlane::session
{

struct AiConfigSpec
{
    std::string apiUrl;
    std::string apiKey;
    std::string model;
    std::string systemPrompt;
    double temperature;
};

struct ParsedTaskConfig
{
    uint8_t aiCount;
    bool hasJudge;
    uint8_t maxRounds;
    uint16_t maxResponseLength;
    uint32_t timeoutMs;
    uint32_t maxCharPerTopic;
    std::vector<AiConfigSpec> configs;
};

class AiAgora : public fw::EoBase<AiAgora>
{
  public:
    explicit AiAgora(fw::EoConfig &cfg, fw::EoAddress sessionDispatcherAddr,
                     fw::EoAddress sessionMgrAddr, fw::EoAddress routerAddr,
                     fw::EoAddress accessGatewayAddr, common::TaskPool &pool);

    void handle(const common::message::TempConfig &msg);
    void handle(const common::message::TaskConfigReq &req);
    void handle(const common::message::AiChatConfigResp &resp);

  protected:
    void init() override
    {
        onMsg<common::message::TempConfig>();
        onMsg<common::message::TaskConfigReq>();
        onMsg<common::message::AiChatConfigResp>();
    }

  private:
    static constexpr uint8_t kStateUnconfigured = 0;
    static constexpr uint8_t kStateConfiguring = 1;
    static constexpr uint8_t kStateWaitingForTopic = 2;
    static constexpr uint8_t kStateWaitingForDebateReplies = 3;
    static constexpr uint8_t kStateWaitingForJudgeVerdict = 4;
    static constexpr uint8_t kStateDead = 5;

    static constexpr uint16_t kJudgePendingBit = 0x8000;

    using ContextType = common::context::AiAgoraContext;

    void processConfig(ContextType &ctx, const common::message::TaskConfigReq &req);
    void onBusTaskCreated(ParsedTaskConfig cfg, const common::message::BusTaskCreateResp &resp);
    void sendAiChatConfigs(const common::message::UserHead &head, ContextType &ctx,
                           const ParsedTaskConfig &cfg);
    void onAiChatConfigResp(ContextType &ctx, const common::message::AiChatConfigResp &resp);
    void failConfig(const common::message::UserHead &head);
    void completeConfig(const common::message::UserHead &head, ContextType &ctx);
    static void applyBusTaskIds(ContextType &ctx, const ParsedTaskConfig &cfg,
                                const std::vector<common::GTID> &busTaskIds);
    void recycleBusTasks(const common::message::UserHead &head, ContextType &ctx);
    static void clearPendingBit(ContextType &ctx, uint8_t aiIndex);
    static utils::Result<ParsedTaskConfig> validateAndExtractConfig(const std::string &payload);
    static std::vector<common::TaskType> buildTaskTypes(const ParsedTaskConfig &cfg);
    static uint16_t buildPendingBits(const ParsedTaskConfig &cfg);

    fw::EoAddress sessionMgrAddr_;
    fw::EoAddress routerAddr_;
    fw::EoAddress accessGatewayAddr_;
    common::TaskPool &pool_;
};

} // namespace DPlane::session
