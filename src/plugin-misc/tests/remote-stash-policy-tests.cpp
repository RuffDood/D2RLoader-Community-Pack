#include "remote-stash-policy.h"
#include "remote-stash-layout-policy.h"

#include "../../../tests/test-check.h"

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

int main() {
    using namespace ruffneckk::remote_stash;

    const auto defaults = ParseHotkeyConfig(nlohmann::json::object());
    TEST_REQUIRE(!defaults.enabled);
    TEST_REQUIRE(defaults.hotkeyText == "None");
    TEST_REQUIRE(defaults.hotkey.virtualKey == 0);

    const auto enabled = ParseHotkeyConfig(nlohmann::json::parse(
        R"json({"enabled":true,"hotkey":"SHIFT+S"})json"));
    TEST_REQUIRE(enabled.enabled);
    TEST_REQUIRE(enabled.hotkey.virtualKey == 'S');
    TEST_REQUIRE(enabled.hotkey.shift);
    TEST_REQUIRE(!enabled.hotkey.control);

    TEST_REQUIRE(Throws([] {
        ParseHotkeyConfig(nlohmann::json::parse(
            R"json({"enabled":true,"hotkey":"None"})json"));
    }));
    TEST_REQUIRE(Throws([] {
        ParseHotkeyConfig(nlohmann::json::parse(
            R"json({"enabled":true,"hotkey":"S","unknown":1})json"));
    }));
    TEST_REQUIRE(Throws([] {
        ParseHotkeyConfig(nlohmann::json::parse(
            R"json({"enabled":"yes","hotkey":"S"})json"));
    }));

    constexpr std::uintptr_t remoteStashButton = 0x3000;
    constexpr std::size_t onClickMessageOffset = 0x558;
    constexpr std::uintptr_t remoteStashMessage =
        remoteStashButton + onClickMessageOffset;
    TEST_REQUIRE(IsExpectedEmbeddedMessage(
        remoteStashMessage,
        remoteStashMessage
    ));
    TEST_REQUIRE(!IsExpectedEmbeddedMessage(
        remoteStashMessage + 1,
        remoteStashMessage
    ));
    TEST_REQUIRE(!IsExpectedEmbeddedMessage(0, remoteStashMessage));
    TEST_REQUIRE(!IsExpectedEmbeddedMessage(remoteStashMessage, 0));
    return 0;
}
