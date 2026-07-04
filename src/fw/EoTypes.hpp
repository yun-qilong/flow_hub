// src/fw/EoTypes.hpp
// CAF type aliases — keep framework-specific types here.
// Renamed to use EO terminology per project convention.
//
// EoSystem 已升级为包装类，见 fw/EoEnv.hpp。
// 此文件仅保留轻量类型别名。

#pragma once

#include "caf/all.hpp"

namespace fw
{

using EoAddress = caf::actor;
using MessageHandler = caf::message_handler;
using EoConfig = caf::actor_config;
using EoSystemConfig = caf::actor_system_config;
using EoDuration = caf::timespan;

} // namespace fw
