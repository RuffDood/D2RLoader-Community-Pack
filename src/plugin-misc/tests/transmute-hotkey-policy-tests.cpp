#include "transmute-hotkey-policy.h"

#include "../../../tests/test-check.h"
#include <fstream>
#include <stdexcept>

namespace {

template<class Callback>
bool Throws(Callback&& callback) {
    try {
        callback();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::TransmuteHotkey;

    Hotkey hotkey{};
    TEST_REQUIRE(ParseHotkey("CTRL+SHIFT+T", hotkey));
    TEST_REQUIRE(hotkey.virtualKey == 'T');
    TEST_REQUIRE(hotkey.device == InputDevice::Keyboard);
    TEST_REQUIRE(hotkey.control && hotkey.shift && !hotkey.alt);
    TEST_REQUIRE(ExactModifiersMatch(hotkey, true, true, false));
    TEST_REQUIRE(!ExactModifiersMatch(hotkey, true, true, true));
    TEST_REQUIRE(ParseHotkey("MOUSE4", hotkey));
    TEST_REQUIRE(hotkey.virtualKey == 0x05 && IsMouseHotkey(hotkey));
    TEST_REQUIRE(ParseHotkey("ctrl + mouse 5", hotkey));
    TEST_REQUIRE(hotkey.virtualKey == 0x06 && hotkey.control);
    TEST_REQUIRE(ParseHotkey("T", hotkey));
    TEST_REQUIRE(hotkey.virtualKey == 'T' && !hotkey.control && !hotkey.shift && !hotkey.alt);
    TEST_REQUIRE(ParseHotkey("SHIFT+T", hotkey));
    TEST_REQUIRE(hotkey.virtualKey == 'T' && hotkey.shift);
    TEST_REQUIRE(!ParseHotkey("CTRL+CTRL+T", hotkey));
    TEST_REQUIRE(!ParseHotkey("T+H", hotkey));
    TEST_REQUIRE(!ParseHotkey("F25", hotkey));

    TEST_REQUIRE(IsFreshRequest(1'100, 1'000, 250));
    TEST_REQUIRE(!IsFreshRequest(1'251, 1'000, 250));

    const auto missing = ParseConfig(nlohmann::json::object());
    TEST_REQUIRE(!missing.enabled);
    TEST_REQUIRE(missing.hotkeyText == "CTRL+SHIFT+T");
    const auto enabled = ParseConfig(nlohmann::json::parse(
        R"json({"transmuteHotkey":{"enabled":true,"hotkey":"MOUSE4"}})json"));
    TEST_REQUIRE(enabled.enabled && IsMouseHotkey(enabled.hotkey));
    TEST_REQUIRE(Throws([] {
        ParseConfig(nlohmann::json::parse(
            R"json({"transmuteHotkey":{"enabled":true,"unknown":1}})json"));
    }));
    const auto singleKey = ParseConfig(nlohmann::json::parse(
        R"json({"transmuteHotkey":{"enabled":true,"hotkey":"T"}})json"));
    TEST_REQUIRE(singleKey.enabled && singleKey.hotkey.virtualKey == 'T');

    TEST_REQUIRE(argc == 2);
    std::ifstream shippedConfig(argv[1]);
    TEST_REQUIRE(shippedConfig.is_open());
    const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
    const auto shipped = ParseConfig(root.at("misc"));
    TEST_REQUIRE(!shipped.enabled);
    TEST_REQUIRE(shipped.hotkeyText == "CTRL+SHIFT+T");
    return 0;
}
