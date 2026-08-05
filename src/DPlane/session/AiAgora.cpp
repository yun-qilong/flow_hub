#include "DPlane/session/AiAgora.hpp"

namespace DPlane::session
{

AiAgora::AiAgora(fw::EoConfig &cfg) : fw::EoBase<AiAgora>(cfg) {}

void AiAgora::handle(const common::message::TempConfig & /*msg*/) {}

} // namespace DPlane::session
