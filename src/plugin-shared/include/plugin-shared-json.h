#pragma once

#include <D2RLPlugin/context.h>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace PSh_Json_Detail {

inline constexpr wchar_t ConfigFileName[] = L"D2RPlugins.json";

struct ConfigPaths {
	std::optional<std::filesystem::path> modConfig;
	std::filesystem::path globalConfig;
};

inline bool HasPath(const wchar_t* path) noexcept
{
	return path != nullptr && path[0] != L'\0';
}

// D2RLoader exposes the active mod support directory as
// <D2R>/mods/<mod>/d2rloader. Derive the game root from that runtime-owned
// path instead of relying on the process working directory.
inline std::filesystem::path GameRootFromModSupportDirectory(
	const std::filesystem::path& modSupportDirectory)
{
	auto gameRoot = modSupportDirectory.parent_path(); // <D2R>/mods/<mod>
	gameRoot = gameRoot.parent_path();                  // <D2R>/mods
	gameRoot = gameRoot.parent_path();                  // <D2R>
	if (gameRoot.empty()) {
		throw std::runtime_error(
			"PluginPack: D2RLoader supplied an invalid mod support directory.");
	}
	return gameRoot;
}

inline ConfigPaths ResolveConfigPaths(const D2RL::PluginContext* context)
{
	if (!context) {
		throw std::runtime_error(
			"PluginPack: D2RLoader did not supply a plugin context for configuration resolution.");
	}

	ConfigPaths paths;
	std::filesystem::path gameRoot;

	if (context->activeMod && context->activeMod[0] != '\0'
		&& HasPath(context->modSupportDirectory)) {
		const std::filesystem::path modSupportDirectory(context->modSupportDirectory);
		paths.modConfig = modSupportDirectory / L"config" / ConfigFileName;
		gameRoot = GameRootFromModSupportDirectory(modSupportDirectory);
	}

	if (gameRoot.empty() && HasPath(context->scopeRootDirectory)) {
		const std::filesystem::path scopeRoot(context->scopeRootDirectory);
		if (context->loadScope == D2RL::LoadScope::Global) {
			gameRoot = scopeRoot;
		}
		else if (context->loadScope == D2RL::LoadScope::Mod) {
			// A mod scope root is <D2R>/mods/<mod>.
			gameRoot = scopeRoot.parent_path().parent_path();
		}
	}

	if (gameRoot.empty() && context->loadScope == D2RL::LoadScope::Global
		&& HasPath(context->pluginConfigPath)) {
		// The loader's per-plugin config path lives in the global
		// <D2R>/d2rloader/config directory.
		paths.globalConfig =
			std::filesystem::path(context->pluginConfigPath).parent_path()
			/ ConfigFileName;
	}
	else if (!gameRoot.empty()) {
		paths.globalConfig = gameRoot / L"d2rloader" / L"config" / ConfigFileName;
	}

	if (paths.globalConfig.empty()) {
		throw std::runtime_error(
			"PluginPack: D2RLoader did not supply enough path context to locate the global configuration.");
	}
	return paths;
}

struct FileAttempt {
	bool found{};
	std::optional<nlohmann::json> value;
};

inline FileAttempt TryLoadFile(const std::filesystem::path& path)
{
	std::error_code fileError;
	const bool exists = std::filesystem::exists(path, fileError);
	if (fileError) {
		throw std::runtime_error(
			"PluginPack: could not inspect configuration file '" + path.string()
			+ "': " + fileError.message());
	}
	if (!exists) {
		return {};
	}

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error(
			"PluginPack: configuration file '" + path.string() + "' could not be opened.");
	}

	nlohmann::json parsed;
	try {
		parsed = nlohmann::json::parse(file, nullptr, true, true);
	}
	catch (const std::exception& error) {
		throw std::runtime_error(
			"PluginPack: configuration file '" + path.string() + "' is invalid: " + error.what());
	}

	if (!parsed.is_object()) {
		throw std::runtime_error(
			"PluginPack: configuration file '" + path.string() + "' must contain a JSON object.");
	}

	return { true, std::move(parsed) };
}

inline std::optional<nlohmann::json> LoadConfigFromPaths(
	const std::optional<std::filesystem::path>& modConfig,
	const std::filesystem::path& globalConfig,
	std::filesystem::path* loadedPath = nullptr)
{
	if (modConfig) {
		auto attempt = TryLoadFile(*modConfig);
		if (attempt.found) {
			if (loadedPath) {
				*loadedPath = *modConfig;
			}
			return std::move(attempt.value);
		}
	}

	auto attempt = TryLoadFile(globalConfig);
	if (attempt.found && loadedPath) {
		*loadedPath = globalConfig;
	}
	return attempt.found ? std::move(attempt.value) : std::nullopt;
}

// Community Pack 1.0.0 owns hotkey capture internally. Strip the retired
// field before strict feature parsers inspect an older configuration, so it
// remains harmless without surviving as a configurable policy.
inline void RemoveLegacyHotkeyCaptureOption(nlohmann::json& value)
{
	if (value.is_object()) {
		value.erase("consume");
		for (auto& [key, child] : value.items()) {
			(void)key;
			RemoveLegacyHotkeyCaptureOption(child);
		}
	}
	else if (value.is_array()) {
		for (auto& child : value) {
			RemoveLegacyHotkeyCaptureOption(child);
		}
	}
}

} // namespace PSh_Json_Detail

// Tries <D2R>/mods/<mod>/d2rloader/config/D2RPlugins.json, then
// <D2R>/d2rloader/config/D2RPlugins.json.
// A missing file is allowed. A present but unreadable or invalid file is rejected,
// and an invalid mod-local file never silently falls back to the global file.
inline std::optional<nlohmann::json> PSh_Json_LoadConfig(const D2RL::PluginContext* context)
{
	const auto paths = PSh_Json_Detail::ResolveConfigPaths(context);
	std::filesystem::path loadedPath;
	auto config = PSh_Json_Detail::LoadConfigFromPaths(
		paths.modConfig,
		paths.globalConfig,
		&loadedPath);
	if (config) {
		PSh_Json_Detail::RemoveLegacyHotkeyCaptureOption(*config);
	}
	if (context) {
		std::string message;
		if (config) {
			message = "PluginPack: loaded configuration from '"
				+ loadedPath.string() + "'.";
		}
		else {
			message = "PluginPack: no D2RPlugins.json found; using built-in defaults. Global path: '"
				+ paths.globalConfig.string() + "'.";
		}
		context->LogInfo(message.c_str());
	}
	return config;
}

// Returns the named top-level section from a loaded config, or an empty object if missing.
// A present section of the wrong JSON type is rejected instead of being treated as absent.
inline nlohmann::json PSh_Json_GetSection(
	const std::optional<nlohmann::json>& cfg,
	const char* section)
{
	if (!cfg) {
		return nlohmann::json::object();
	}

	const auto entry = cfg->find(section);
	if (entry == cfg->end()) {
		return nlohmann::json::object();
	}
	if (!entry->is_object()) {
		throw std::runtime_error(
			std::string("PluginPack: configuration section '") + section + "' must be a JSON object.");
	}
	return *entry;
}

inline void PSh_Json_LogConfigError(
	const D2RL::PluginContext* context,
	const std::exception& error) noexcept
{
	if (context) {
		context->LogError(error.what());
	}
}
