#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

namespace RuffnecKk::FloatingDamageFeature {

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept;
void Unload() noexcept;

} // namespace RuffnecKk::FloatingDamageFeature
