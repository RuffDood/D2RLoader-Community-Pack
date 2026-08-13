#include <plugin-shared-json.h>

#include "../../../tests/test-check.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

void WriteFile(const std::filesystem::path& path, std::string_view contents)
{
	std::ofstream file(path, std::ios::binary);
	TEST_REQUIRE(file.is_open());
	file << contents;
	TEST_REQUIRE(file.good());
}

template <typename Callback>
bool Throws(Callback&& callback)
{
	try {
		callback();
		return false;
	}
	catch (const std::exception&) {
		return true;
	}
}

} // namespace

int main()
{
	{
		D2RL::PluginContext globalContext{};
		globalContext.contextSize = D2RL::PluginContextSize;
		globalContext.loadScope = D2RL::LoadScope::Global;
		globalContext.scopeRootDirectory = L"C:\\Games\\Diablo II Resurrected";
		const auto paths = PSh_Json_Detail::ResolveConfigPaths(&globalContext);
		TEST_REQUIRE(!paths.modConfig);
		TEST_REQUIRE(paths.globalConfig == std::filesystem::path(
			L"C:\\Games\\Diablo II Resurrected\\d2rloader\\config\\D2RPlugins.json"));
	}

	{
		D2RL::PluginContext modContext{};
		modContext.contextSize = D2RL::PluginContextSize;
		modContext.loadScope = D2RL::LoadScope::Mod;
		modContext.activeMod = "BKVince";
		modContext.scopeRootDirectory =
			L"C:\\Games\\Diablo II Resurrected\\mods\\BKVince";
		modContext.modSupportDirectory =
			L"C:\\Games\\Diablo II Resurrected\\mods\\BKVince\\d2rloader";
		const auto paths = PSh_Json_Detail::ResolveConfigPaths(&modContext);
		TEST_REQUIRE(paths.modConfig);
		TEST_REQUIRE(*paths.modConfig == std::filesystem::path(
			L"C:\\Games\\Diablo II Resurrected\\mods\\BKVince\\d2rloader\\config\\D2RPlugins.json"));
		TEST_REQUIRE(paths.globalConfig == std::filesystem::path(
			L"C:\\Games\\Diablo II Resurrected\\d2rloader\\config\\D2RPlugins.json"));
	}

	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path()
		/ ("pluginpack-json-tests-" + std::to_string(unique));
	const auto modDirectory = root / "mod";
	const auto globalDirectory = root / "global";
	std::filesystem::create_directories(modDirectory);
	std::filesystem::create_directories(globalDirectory);

	const auto modConfig = modDirectory / "D2RPlugins.json";
	const auto globalConfig = globalDirectory / "D2RPlugins.json";

	const auto absent = PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig);
	TEST_REQUIRE(!absent);

	WriteFile(globalConfig, R"json({
		// JSON comments remain supported.
		"items": { "gambleScreenLimit": { "enabled": true } }
	})json");
	const auto global = PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig);
	TEST_REQUIRE(global);
	TEST_REQUIRE(PSh_Json_GetSection(global, "items").at("gambleScreenLimit").at("enabled") == true);

	WriteFile(modConfig, R"json({ "items": { "gambleScreenLimit": { "enabled": false } } })json");
	const auto local = PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig);
	TEST_REQUIRE(local);
	TEST_REQUIRE(PSh_Json_GetSection(local, "items").at("gambleScreenLimit").at("enabled") == false);

	auto legacyHotkey = nlohmann::json::parse(R"json({
		"misc": { "transmuteHotkey": { "enabled": true, "consume": false } }
	})json");
	PSh_Json_Detail::RemoveLegacyHotkeyCaptureOption(legacyHotkey);
	TEST_REQUIRE(!legacyHotkey.at("misc").at("transmuteHotkey").contains("consume"));
	TEST_REQUIRE(legacyHotkey.at("misc").at("transmuteHotkey").at("enabled") == true);

	WriteFile(modConfig, "{ invalid json");
	TEST_REQUIRE(Throws([&] { (void)PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig); }));

	std::filesystem::remove(modConfig);
	WriteFile(globalConfig, "[]");
	TEST_REQUIRE(Throws([&] { (void)PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig); }));

	WriteFile(globalConfig, R"json({ "items": false })json");
	const auto wrongSection = PSh_Json_Detail::LoadConfigFromPaths(modConfig, globalConfig);
	TEST_REQUIRE(Throws([&] { (void)PSh_Json_GetSection(wrongSection, "items"); }));

	std::filesystem::remove_all(root);
	return 0;
}
