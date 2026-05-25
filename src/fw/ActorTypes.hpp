// src/fw/ActorTypes.hpp
// CAF type aliases — keep framework-specific types here.

#pragma once

#include "caf/all.hpp"

namespace fw
{

using ActorRef = caf::actor;
using ActorConfig = caf::actor_config;
using ActorSystem = caf::actor_system;
using ActorSysConfig = caf::actor_system_config;

} // namespace fw
