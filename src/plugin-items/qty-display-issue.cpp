#include "qty-display-issue.h"
#include <plugin-shared.h>
#include "qty-display-issue-policy.h"

#include <cstdio>
#include <string>

namespace RuffnecKk::QtyDisplayIssue {
namespace {

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
		"Qty Display Fix 1.1.0 (plugin-items): enabled=%s; placement=vanilla.",
		Settings.enabled ? "true" : "false"
	);
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

bool InstallPatch() noexcept {
	const auto replacement = BuildQuantitySuppressionPatch();
	if (!PSh_ManifestPatchBytes(Context, PSH_MANIFEST_SITE("items.qtyDisplayIssue.quantitySuppressionBranch"),
			QuantitySuppressionSignatureRva,
			QuantitySuppressionExpected.data(),
			static_cast<std::uint32_t>(QuantitySuppressionExpected.size()),
			replacement.data(),
			static_cast<std::uint32_t>(replacement.size()))) {
		Context->LogError(
			"plugin-items: Qty Display Fix socketed-quantity signature mismatch."
		);
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
		const auto message = std::string("plugin-items: invalid items.qtyDisplayIssue (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (Settings.enabled && !InstallPatch()) return false;

	if (!PSh_RegisterConsoleCommand(context,
			"qty-display-issue",
			Status,
			"Show the socketed-stackable quantity fix status.")) {
		context->LogWarn(
			"plugin-items: Qty Display Fix status command was not registered."
		);
	}

	char message[208]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Qty Display Fix 1.1.0 by RuffnecKk loaded: enabled=%s; "
		"config=items.qtyDisplayIssue.",
		Settings.enabled ? "true" : "false"
	);
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	Settings = {};
	Context = nullptr;
}

} // namespace RuffnecKk::QtyDisplayIssue
