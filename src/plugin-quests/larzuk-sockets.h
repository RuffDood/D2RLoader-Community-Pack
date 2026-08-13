#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

namespace RuffnecKk::ForceLarzukSockets {

bool Load(const D2RL::PluginContext* context, const nlohmann::json& questsConfig) noexcept;
void Unload() noexcept;

} // namespace RuffnecKk::ForceLarzukSockets
