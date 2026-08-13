#pragma once

#include <plugin-shared.h>
#include <plugin-shared-json.h>

struct LevelPluginOptions {
	bool bDisableAct1Path;

	void Load(const D2RL::PluginContext* context, const nlohmann::json& cfg);
};
