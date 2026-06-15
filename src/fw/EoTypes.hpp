// src/fw/EoTypes.hpp
// CAF type aliases — keep framework-specific types here.
// Renamed to use EO terminology per project convention.

#pragma once

#include "caf/all.hpp"

namespace fw
{

using EoAddress = caf::actor;
using EoConfig = caf::actor_config;
using EoSystem = caf::actor_system;
using EoSystemConfig = caf::actor_system_config;

} // namespace fw
