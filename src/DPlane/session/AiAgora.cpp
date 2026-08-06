#include "DPlane/session/AiAgora.hpp"

#include <utility>

namespace DPlane::session
{

AiAgora::AiAgora(fw::EoConfig &cfg, fw::EoAddress sessionDispatcherAddr)
    : fw::EoBase<AiAgora>(cfg)
{
    sendTo(std::move(sessionDispatcherAddr), common::message::TempConfig{7});
}

void AiAgora::handle(const common::message::TempConfig & /*msg*/) {}

} // namespace DPlane::session
