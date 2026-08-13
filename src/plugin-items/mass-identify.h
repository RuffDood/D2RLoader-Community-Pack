#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

namespace RuffnecKk::MassIdentify {

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& itemsConfig) noexcept;
void Activate() noexcept;
void Unload() noexcept;

} // namespace RuffnecKk::MassIdentify
