#include "gamble-screen-limit.h"
#include <plugin-shared.h>
#include "gamble-screen-limit-policy.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace RuffnecKk::GambleScreenLimit {
namespace {

constexpr std::uintptr_t GambleLoopSignatureRva = 0x541A7C;
constexpr std::uintptr_t GambleLoopLimitRva = 0x541A7E;
constexpr std::array<std::uint8_t, 9> GambleLoopExpected{
	0x83, 0xFD, 0x0E, 0x0F, 0x8C, 0xDB, 0xFE, 0xFF, 0xFF
};
constexpr std::array<std::uint8_t, 1> GambleLimitExpected{VanillaLimit};

const D2RL::PluginContext* Context{};
Config Settings{};

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char message[192]{};
	std::snprintf(
		message,
		sizeof(message),
		"Gamble Screen Limit 1.2.0 (plugin-items): enabled=%s; effective=%u; "
		"vanilla=%u; expanded=%u (fixed).",
		Settings.enabled ? "true" : "false",
		static_cast<unsigned>(EffectiveLimit(Settings)),
		static_cast<unsigned>(VanillaLimit),
		static_cast<unsigned>(ExpandedLimit)
	);
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

bool InstallPatch() noexcept {
	if (!Context->CheckExpectedBytes(
			GambleLoopSignatureRva,
			GambleLoopExpected.data(),
			static_cast<std::uint32_t>(GambleLoopExpected.size()))) {
		Context->LogError(
			"plugin-items: Gamble Screen Limit full loop signature mismatch."
		);
		return false;
	}
	if (!PSh_ManifestPatchWriteU8(Context, PSH_MANIFEST_SITE("items.gambleScreenLimit.generationLimit"),
			GambleLoopLimitRva,
			GambleLimitExpected.data(),
			static_cast<std::uint32_t>(GambleLimitExpected.size()),
			ExpandedLimit)) {
		Context->LogError("plugin-items: Gamble Screen Limit patch failed.");
		return false;
	}
	return true;
}

} // namespace

bool Load(const D2RL::PluginContext* context, const nlohmann::json& itemsConfig) noexcept {
	if (!context) return false;
	Context = context;

	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string("plugin-items: invalid items.gambleScreenLimit (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (Settings.enabled && !InstallPatch()) return false;

	if (!PSh_RegisterConsoleCommand(context,
			"gamble-screen-limit",
			Status,
			"Show the configured gambling-screen generation limit.")) {
		context->LogWarn(
			"plugin-items: Gamble Screen Limit status command was not registered."
		);
	}

	char message[224]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Gamble Screen Limit 1.2.0 by RuffnecKk loaded: enabled=%s; "
		"effective limit=%u; config=items.gambleScreenLimit.",
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

} // namespace RuffnecKk::GambleScreenLimit
