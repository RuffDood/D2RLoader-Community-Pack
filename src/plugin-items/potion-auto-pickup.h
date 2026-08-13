#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

namespace RuffnecKk::PotionAutoPickUp {

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& itemsConfig) noexcept;
void Unload() noexcept;

} // namespace RuffnecKk::PotionAutoPickUp
