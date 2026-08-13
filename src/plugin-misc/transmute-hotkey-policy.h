#pragma once

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::TransmuteHotkey {

enum class InputDevice : std::uint8_t {
    Keyboard,
    Mouse,
};

struct Hotkey {
    std::uint32_t virtualKey{};
    InputDevice device{InputDevice::Keyboard};
    bool control{};
    bool shift{};
    bool alt{};
};

struct Config {
    bool enabled{};
    Hotkey hotkey{'T', InputDevice::Keyboard, true, true, false};
    std::string hotkeyText{"CTRL+SHIFT+T"};
};

inline std::string UpperTrim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    std::string result(value.substr(first, last - first + 1));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

inline bool ParseMainKey(
    const std::string& token,
    std::uint32_t& virtualKey,
    InputDevice& device
) {
    if (token.size() == 1) {
        const auto ch = static_cast<unsigned char>(token.front());
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            virtualKey = ch;
            device = InputDevice::Keyboard;
            return true;
        }
    }
    if (token.size() >= 2 && token.front() == 'F') {
        unsigned value{};
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < '0' || token[index] > '9') return false;
            value = value * 10 + static_cast<unsigned>(token[index] - '0');
        }
        if (value >= 1 && value <= 24) {
            virtualKey = 0x70U + value - 1U;
            device = InputDevice::Keyboard;
            return true;
        }
    }
    struct NamedKey {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedKey namedKeys[]{
        {"SPACE", 0x20}, {"TAB", 0x09}, {"INSERT", 0x2D},
        {"DELETE", 0x2E}, {"HOME", 0x24}, {"END", 0x23},
        {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22},
    };
    for (const auto& key : namedKeys) {
        if (token == key.name) {
            virtualKey = key.virtualKey;
            device = InputDevice::Keyboard;
            return true;
        }
    }
    struct NamedMouseButton {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedMouseButton mouseButtons[]{
        {"MOUSE3", 0x04}, {"MOUSE 3", 0x04}, {"MIDDLE", 0x04},
        {"MBUTTON", 0x04},
        {"MOUSE4", 0x05}, {"MOUSE 4", 0x05}, {"XBUTTON1", 0x05},
        {"MOUSE5", 0x06}, {"MOUSE 5", 0x06}, {"XBUTTON2", 0x06},
    };
    for (const auto& button : mouseButtons) {
        if (token == button.name) {
            virtualKey = button.virtualKey;
            device = InputDevice::Mouse;
            return true;
        }
    }
    return false;
}

inline bool ParseHotkey(std::string_view text, Hotkey& hotkey) {
    Hotkey parsed{};
    bool hasMainKey{};
    std::size_t begin{};
    while (begin <= text.size()) {
        const auto separator = text.find('+', begin);
        const auto token = UpperTrim(text.substr(
            begin,
            separator == std::string_view::npos
                ? text.size() - begin
                : separator - begin));
        if (token.empty()) return false;
        if (token == "CTRL" || token == "CONTROL") {
            if (parsed.control) return false;
            parsed.control = true;
        } else if (token == "SHIFT") {
            if (parsed.shift) return false;
            parsed.shift = true;
        } else if (token == "ALT") {
            if (parsed.alt) return false;
            parsed.alt = true;
        } else {
            if (hasMainKey || !ParseMainKey(
                    token, parsed.virtualKey, parsed.device)) {
                return false;
            }
            hasMainKey = true;
        }
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    if (!hasMainKey || parsed.virtualKey == 0) return false;
    hotkey = parsed;
    return true;
}

inline bool IsMouseHotkey(const Hotkey& hotkey) noexcept {
    return hotkey.device == InputDevice::Mouse;
}

inline bool ExactModifiersMatch(
    const Hotkey& hotkey,
    bool control,
    bool shift,
    bool alt
) noexcept {
    return hotkey.control == control
        && hotkey.shift == shift
        && hotkey.alt == alt;
}

inline bool IsFreshRequest(
    std::uint64_t now,
    std::uint64_t requestedAt,
    std::uint64_t maximumAge
) noexcept {
    return requestedAt != 0 && now >= requestedAt && now - requestedAt <= maximumAge;
}

inline Config ParseConfig(const nlohmann::json& miscConfig) {
    if (!miscConfig.is_object()) {
        throw std::invalid_argument("misc must be an object");
    }
    Config parsed{};
    const auto entry = miscConfig.find("transmuteHotkey");
    if (entry == miscConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument("misc.transmuteHotkey must be an object");
    }
    for (const auto& [key, value] : entry->items()) {
        (void)value;
        if (key != "enabled" && key != "hotkey") {
            throw std::invalid_argument(
                "misc.transmuteHotkey contains unknown key '" + key + "'");
        }
    }
    if (entry->contains("enabled") && !entry->at("enabled").is_boolean()) {
        throw std::invalid_argument("misc.transmuteHotkey.enabled must be a boolean");
    }
    if (entry->contains("hotkey") && !entry->at("hotkey").is_string()) {
        throw std::invalid_argument("misc.transmuteHotkey.hotkey must be a string");
    }
    parsed.enabled = entry->value("enabled", false);
    parsed.hotkeyText = entry->value("hotkey", std::string("CTRL+SHIFT+T"));
    if (!ParseHotkey(parsed.hotkeyText, parsed.hotkey)) {
        throw std::invalid_argument(
            "misc.transmuteHotkey.hotkey is invalid or unsupported");
    }
    return parsed;
}

} // namespace RuffnecKk::TransmuteHotkey
