#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

// RuffnecKk ethereal item features owned by eezstreet's plugin-items module.
bool ItemsEthereal_Install(
	const D2RL::PluginContext* context,
	const nlohmann::json& itemsConfig
) noexcept;

void ItemsEthereal_Reset() noexcept;
