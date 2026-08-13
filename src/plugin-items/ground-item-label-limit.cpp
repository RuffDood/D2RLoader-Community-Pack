#include "ground-item-label-limit.h"
#include <plugin-shared.h>
#include "ground-item-label-limit-policy.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace RuffnecKk::GroundItemLabelLimit {
namespace {

struct Signature {
	std::uintptr_t rva;
	std::array<std::uint8_t, 14> expected;
	std::uint32_t size;
};

struct PatchSite {
	const char* manifestId;
	std::uintptr_t rva;
	std::array<std::uint8_t, 6> expected;
	std::array<std::uint8_t, 6> limit64;
	std::array<std::uint8_t, 6> limit128;
	std::uint32_t size;
};

constexpr std::array<Signature, 7> Signatures{{
	{0x1516EBE, {0x48, 0x83, 0xF8, 0x20, 0x76, 0x70, 0x49, 0x8B, 0x0E}, 9},
	{0x1516ECE, {0x48, 0x8D, 0xB1, 0x80, 0x28, 0x00, 0x00, 0x4C, 0x8D, 0x0C, 0x0F}, 11},
	{0x1516F41, {0x41, 0xB9, 0x20, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x55, 0x88}, 10},
	{0x1519A14, {0x49, 0x83, 0xBE, 0x28, 0x29, 0x00, 0x00, 0x20, 0x44, 0x0F, 0x28, 0x5C, 0x24, 0x70}, 14},
	{0x1519A4F, {0x48, 0x83, 0xF8, 0x20, 0x73, 0x5B, 0xE8, 0x66, 0xC9, 0xCE, 0xFF}, 11},
	{0x1519AAA, {0x48, 0x83, 0xF8, 0x20, 0x72, 0xA5, 0x76, 0x4F}, 8},
	{0x1519AF9, {0x49, 0x83, 0x7C, 0x24, 0x10, 0x20, 0x77, 0xB1}, 8},
}};

constexpr std::array<PatchSite, 7> PatchSites{{
	{PSH_MANIFEST_SITE("items.groundItemLabels.recordCount"), 0x1516EBE, {0x48, 0x83, 0xF8, 0x20}, {0x48, 0x83, 0xF8, 0x40}, {0x66, 0x3D, 0x80, 0x00}, 4},
	{PSH_MANIFEST_SITE("items.groundItemLabels.recordEndpoint"), 0x1516ED1, {0x80, 0x28, 0x00, 0x00}, {0x00, 0x51, 0x00, 0x00}, {0x00, 0xA2, 0x00, 0x00}, 4},
	{PSH_MANIFEST_SITE("items.groundItemLabels.recordFillCount"), 0x1516F43, {0x20, 0x00, 0x00, 0x00}, {0x40, 0x00, 0x00, 0x00}, {0x80, 0x00, 0x00, 0x00}, 4},
	{PSH_MANIFEST_SITE("items.groundItemLabels.layoutSourceCount"), 0x1519A1B, {0x20}, {0x40}, {0x7F}, 1},
	{PSH_MANIFEST_SITE("items.groundItemLabels.layoutInitialCount"), 0x1519A4F, {0x48, 0x83, 0xF8, 0x20}, {0x48, 0x83, 0xF8, 0x40}, {0x66, 0x3D, 0x80, 0x00}, 4},
	{PSH_MANIFEST_SITE("items.groundItemLabels.layoutGrowthCount"), 0x1519AAA, {0x48, 0x83, 0xF8, 0x20}, {0x48, 0x83, 0xF8, 0x40}, {0x66, 0x3D, 0x80, 0x00}, 4},
	{PSH_MANIFEST_SITE("items.groundItemLabels.layoutShrinkCount"), 0x1519AF9, {0x49, 0x83, 0x7C, 0x24, 0x10, 0x20}, {0x49, 0x83, 0x7C, 0x24, 0x10, 0x40}, {0x41, 0x80, 0x7C, 0x24, 0x10, 0x80}, 6},
}};

const D2RL::PluginContext* Context{};
Config Settings{};

bool ValidateAllSites() noexcept {
	if (!Context || Context->exeBase == 0) return false;
	for (const auto& signature : Signatures) {
		if (!Context->CheckExpectedBytes(
				signature.rva,
				signature.expected.data(),
				signature.size)) {
			return false;
		}
	}
	return true;
}

bool InstallPatches() noexcept {
	if (!ValidateAllSites()) {
		Context->LogError(
			"plugin-items: Ground Item Label Limit patch-set signature mismatch; no patch was applied."
		);
		return false;
	}

	for (const auto& site : PatchSites) {
		const auto& replacement = Settings.limit == MaximumExpandedLimit
			? site.limit128
			: site.limit64;
		if (!PSh_ManifestPatchBytes(Context, site.manifestId,
				site.rva,
				site.expected.data(),
				site.size,
				replacement.data(),
				site.size)) {
			Context->LogError(
				"plugin-items: Ground Item Label Limit patch failed."
			);
			return false;
		}
	}
	return true;
}

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char message[224]{};
	std::snprintf(
		message,
		sizeof(message),
		"Ground Item Label Limit 1.2.0 (plugin-items): enabled=%s; effective=%u; "
		"vanilla=%u; configured=%u; allowed=64,128.",
		Settings.enabled ? "true" : "false",
		static_cast<unsigned>(EffectiveLimit(Settings)),
		static_cast<unsigned>(VanillaLimit),
		static_cast<unsigned>(Settings.limit)
	);
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

bool Load(const D2RL::PluginContext* context, const nlohmann::json& itemsConfig) noexcept {
	if (!context) return false;
	Context = context;

	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string(
			"plugin-items: invalid items.groundItemLabels ("
		) + exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (Settings.enabled && !InstallPatches()) return false;

	if (!PSh_RegisterConsoleCommand(context,
			"ground-item-label-limit",
			Status,
			"Show the configured ground item label limit.")) {
		context->LogWarn(
			"plugin-items: Ground Item Label Limit status command was not registered."
		);
	}

	char message[224]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Ground Item Label Limit 1.2.0 by RuffnecKk loaded: enabled=%s; "
		"effective limit=%u; config=items.groundItemLabels.",
		Settings.enabled ? "true" : "false",
		static_cast<unsigned>(EffectiveLimit(Settings))
	);
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	Settings = {};
	Context = nullptr;
}

} // namespace RuffnecKk::GroundItemLabelLimit
