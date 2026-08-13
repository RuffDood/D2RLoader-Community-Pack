#include "magic-find-formula.h"

#include "magic-find-formula-policy.h"
#include <plugin-shared.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

namespace RuffnecKk::MagicFindFormula {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t QualityRollRva = 0x4421B0;
constexpr std::uintptr_t MagicFindReadRva = 0x442362;
constexpr std::uintptr_t NegativeGateRva = 0x4423A6;
constexpr std::uintptr_t UniqueWitnessRva = 0x4423CA;
constexpr std::uintptr_t SetWitnessRva = 0x442467;
constexpr std::uintptr_t RareWitnessRva = 0x4424F4;
constexpr std::uintptr_t MagicLinearRva = 0x442576;

constexpr std::array<std::uint8_t, 38> QualityRollExpected{
	0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44,
	0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
	0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
	0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x44, 0x8B,
	0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 34> MagicFindReadExpected{
	0x45, 0x33, 0xC0, 0x41, 0x8D, 0x56, 0x50, 0x48,
	0x8B, 0xCB, 0xE8, 0xEF, 0x38, 0xEB, 0xFF, 0x01,
	0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B,
	0xCB, 0xE8, 0x40, 0x30, 0x06, 0x00, 0x48, 0x85,
	0xC0, 0x74,
};
constexpr std::array<std::uint8_t, 18> NegativeGateExpected{
	0x85, 0xFF, 0x74, 0x49, 0x83, 0xFF, 0x9C, 0x0F,
	0x8E, 0x23, 0x02, 0x00, 0x00, 0x8B, 0x1E, 0x8D,
	0x6F, 0x64,
};
constexpr std::array<std::uint8_t, 15> UniqueWitnessExpected{
	0x83, 0xFD, 0x6E, 0x7F, 0x04, 0x8B, 0xCD, 0xEB,
	0x12, 0x69, 0xC7, 0xFA, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 15> SetWitnessExpected{
	0x83, 0xFD, 0x6E, 0x7F, 0x04, 0x8B, 0xCD, 0xEB,
	0x12, 0x69, 0xC7, 0xF4, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 15> RareWitnessExpected{
	0x83, 0xFD, 0x6E, 0x7F, 0x04, 0x8B, 0xCD, 0xEB,
	0x12, 0x69, 0xC7, 0x58, 0x02, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 27> MagicLinearExpected{
	0x8B, 0x5E, 0x24, 0x41, 0x8B, 0xC4, 0x99, 0xF7,
	0x7E, 0x28, 0x2B, 0xD8, 0xC1, 0xE3, 0x07, 0x45,
	0x85, 0xF6, 0x74, 0x08, 0x6B, 0xC3, 0x64, 0x99,
	0xF7, 0xFD, 0x8B,
};
constexpr std::array<std::uint8_t, 2> DiminishingBranchExpected{0x7F, 0x04};
constexpr std::array<std::uint8_t, 2> LinearBranchReplacement{0x90, 0x90};

struct PatchSite {
	const char* quality;
	const char* manifestId;
	std::uintptr_t rva;
};

constexpr std::array<PatchSite, 3> PatchSites{{
	{"unique", PSH_MANIFEST_SITE("items.magicFindFormula.unique"), 0x4423CD},
	{"set", PSH_MANIFEST_SITE("items.magicFindFormula.set"), 0x44246A},
	{"rare", PSH_MANIFEST_SITE("items.magicFindFormula.rare"), 0x4424F7},
}};

const D2RL::PluginContext* Context{};
Config Settings{};

template <std::size_t Size>
bool Matches(std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected) noexcept {
	return Context && Context->CheckExpectedBytes(
		rva, expected.data(), static_cast<std::uint32_t>(expected.size()));
}

bool ValidateLinearRuntime() noexcept {
	if (!Matches(QualityRollRva, QualityRollExpected)
		|| !Matches(MagicFindReadRva, MagicFindReadExpected)
		|| !Matches(NegativeGateRva, NegativeGateExpected)
		|| !Matches(UniqueWitnessRva, UniqueWitnessExpected)
		|| !Matches(SetWitnessRva, SetWitnessExpected)
		|| !Matches(RareWitnessRva, RareWitnessExpected)
		|| !Matches(MagicLinearRva, MagicLinearExpected)) {
		Context->LogError(
			"plugin-items: Magic Find Formula signature mismatch; no item patch was applied.");
		return false;
	}
	return true;
}

bool InstallLinearPatches() noexcept {
	if (!ValidateLinearRuntime()) return false;
	for (const auto& site : PatchSites) {
		if (!PSh_ManifestPatchBytes(
				Context,
				site.manifestId,
				site.rva,
				DiminishingBranchExpected.data(),
				static_cast<std::uint32_t>(DiminishingBranchExpected.size()),
				LinearBranchReplacement.data(),
				static_cast<std::uint32_t>(LinearBranchReplacement.size()))) {
			const auto message = std::string(
				"plugin-items: Magic Find Formula failed to patch the ")
				+ site.quality + " branch.";
			Context->LogError(message.c_str());
			return false;
		}
	}
	return true;
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char message[224]{};
	std::snprintf(
		message,
		sizeof(message),
		"Magic Find Formula 0.1.0 by RuffnecKk: mode=%s; linear qualities=unique,set,rare; magic and negative MF remain native.",
		ModeName(Settings.mode).data());
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

bool Load(
	const D2RL::PluginContext* context,
	const nlohmann::json& itemsConfig
) noexcept {
	if (!context || context->exeBase == 0) return false;
	Context = context;
	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string(
			"plugin-items: invalid items.magicFindFormula (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}
	if (context->modDataVersionBuild != 0
		&& context->modDataVersionBuild != SupportedBuild) {
		context->LogError(
			"plugin-items: Magic Find Formula supports only D2R build 92777.");
		return false;
	}
	if (IsLinear(Settings) && !InstallLinearPatches()) return false;

	if (!PSh_RegisterConsoleCommand(
			context,
			"magic-find-formula",
			Status,
			"Show the configured Magic Find formula.")) {
		context->LogWarn(
			"plugin-items: Magic Find Formula status command was not registered.");
	}
	char message[192]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Magic Find Formula 0.1.0 by RuffnecKk loaded: mode=%s; patched branches=%u.",
		ModeName(Settings.mode).data(),
		IsLinear(Settings) ? 3u : 0u);
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	Settings = {};
	Context = nullptr;
}

} // namespace RuffnecKk::MagicFindFormula
