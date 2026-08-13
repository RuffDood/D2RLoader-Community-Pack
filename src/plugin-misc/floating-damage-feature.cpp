#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include <plugin-shared-json.h>
#include <MinHook.h>

#include "floating-damage-feature.h"
#include "floating-damage-render.h"
#include "floating-damage.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t HitpointsCommitContextRva = 0x44D083;
constexpr std::uintptr_t HitpointsCommitCallRva = 0x44D093;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t SetUnitStatRva = 0x2F7D10;
constexpr std::uintptr_t GetClientUnitRva = 0x09A5D0;
constexpr std::uintptr_t UpdateCameraRva = 0x0B9B90;
constexpr std::uintptr_t GetRenderThreadContextRootRva = 0x685750;
constexpr std::uintptr_t ProjectUnitToScreenRva = 0x76A7D0;
constexpr std::uintptr_t GetNativeHeightRva = 0x07F4A0;
constexpr std::uintptr_t GetNativeWidthRva = 0x07F510;
constexpr std::uint16_t CriticalStrikeResultFlag = 0x2000;
constexpr std::uint32_t MonsterUnitType = 1;
constexpr std::int32_t HitPointsStatId = 6;

constexpr std::size_t DamagePhysicalOffset = 0x018;
constexpr std::size_t DamageFireOffset = 0x020;
constexpr std::size_t DamageLightningOffset = 0x02C;
constexpr std::size_t DamageMagicOffset = 0x030;
constexpr std::size_t DamageColdOffset = 0x034;
constexpr std::size_t DamagePoisonOffset = 0x038;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
HMODULE Module{};
std::atomic<std::uint64_t> CapturedEvents{};
std::atomic<std::uint64_t> DisplayedEvents{};
std::atomic<std::uint64_t> ProjectionSuccesses{};
std::atomic<std::uint64_t> ProjectionFailures{};
std::atomic_bool ProjectionReadyLogged{};
std::atomic<bool> OverlayReady{};
HANDLE OverlayStopEvent{};
HANDLE OverlayWorker{};
void* HitpointsCommitRelay{};
bool CameraFrameHookInstalled{};

#pragma pack(push, 1)
struct UnitView {
    std::uint32_t unitType;
    std::uint32_t classId;
    std::uint32_t unitId;
    std::uint32_t mode;
};
#pragma pack(pop)

struct NativeScreenPoint {
    float x;
    float y;
};

using GetUnitStatFn = std::int32_t(__fastcall*)(
    UnitView*, std::int32_t, std::uint16_t) noexcept;
using SetUnitStatFn = void(__fastcall*)(
    UnitView*, std::int32_t, std::int32_t, std::uint16_t) noexcept;
using GetClientUnitFn = UnitView*(__fastcall*)(
    std::uint32_t unitId, std::uint32_t unitType) noexcept;
using ProjectUnitToScreenFn = bool(__fastcall*)(
    void* renderContext,
    UnitView* unit,
    NativeScreenPoint* point,
    bool useUnitHeight) noexcept;
using UpdateCameraFn = void(__fastcall*)() noexcept;
using GetRenderThreadContextRootFn = void*(__fastcall*)() noexcept;
using GetNativeDimensionFn = std::int32_t(__fastcall*)() noexcept;
GetUnitStatFn GetUnitStat{};
SetUnitStatFn SetUnitStat{};
GetClientUnitFn GetClientUnit{};
ProjectUnitToScreenFn ProjectUnitToScreen{};
UpdateCameraFn OriginalUpdateCamera{};
GetRenderThreadContextRootFn GetRenderThreadContextRoot{};
GetNativeDimensionFn GetNativeHeight{};
GetNativeDimensionFn GetNativeWidth{};

constexpr std::size_t ProjectionCacheSize = 8192;
constexpr std::uint64_t ProjectionFreshnessMs = 250;
constexpr std::size_t ProjectionRequestCapacity = 1024;
constexpr std::uint64_t ProjectionRequestLeaseMs = 300;
constexpr std::uint64_t ProjectionSweepIntervalMs = 4;

struct ProjectionCacheEntry {
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> key{};
    std::atomic<std::uint64_t> elevatedPoint{};
    std::atomic<std::uint64_t> elevatedTick{};
    std::atomic<std::uint64_t> basePoint{};
    std::atomic<std::uint64_t> baseTick{};
    std::atomic<std::uint64_t> attemptTick{};
    std::atomic_bool visible{};
};

struct ProjectionRequestSlot {
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> key{};
    std::atomic<std::uint64_t> requestedUntil{};
};

std::array<ProjectionCacheEntry, ProjectionCacheSize> ProjectionCache{};
std::array<ProjectionRequestSlot, ProjectionRequestCapacity>
    ProjectionRequests{};
std::atomic<std::int32_t> CachedNativeWidth{};
std::atomic<std::int32_t> CachedNativeHeight{};
std::atomic<std::uint64_t> NativeDimensionsRefreshTick{};
std::atomic<std::uint64_t> LastProjectionSweepTick{};
std::atomic<std::uint64_t> ActiveProjectionAttempts{};
std::atomic<std::uint64_t> ActiveProjectionMisses{};
std::atomic<std::uint64_t> ProjectionRequestDrops{};
std::atomic<std::uint64_t> CameraFrameTicks{};
std::atomic<std::uint64_t> RenderContextMisses{};
std::atomic_bool CameraFrameReadyLogged{};
thread_local bool ProjectionSweepActive{};

void __cdecl LogOverlayDiagnostic(const char* message) noexcept {
    if (Context && message)
        Context->LogInfo(message);
}

std::string Trim(std::string_view value) {
    std::size_t first{};
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ParseBool(std::string_view value, bool& output) {
    const std::string text = Lower(Trim(value));
    if (text == "true") { output = true; return true; }
    if (text == "false") { output = false; return true; }
    return false;
}

bool ParseInt(std::string_view value, int& output) {
    const std::string text = Trim(value);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ParseFloat(std::string_view value, float& output) {
    const std::string text = Trim(value);
    char* end{};
    output = std::strtof(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

bool ParseColor(std::string_view value, ImVec4& output) {
    std::string text = Trim(value);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') return false;
    text = text.substr(1, text.size() - 2);
    std::array<float, 4> components{};
    std::size_t start{};
    for (std::size_t index = 0; index < components.size(); ++index) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = index + 1 == components.size() ? text.size() : comma;
        if (end == std::string::npos || !ParseFloat(std::string_view(text).substr(start, end - start), components[index])) return false;
        start = end + 1;
    }
    output = ImVec4(components[0], components[1], components[2], components[3]);
    return true;
}

bool ParseTomlString(std::string_view value, std::string& output) {
    const std::string text = Trim(value);
    if (text.size() < 2 || (text.front() != '"' && text.front() != '\'')) return false;
    if (text.back() != text.front()) return false;
    output = text.substr(1, text.size() - 2);
    return !output.empty() && output.size() <= 64;
}

std::string UpperTrim(std::string_view value) {
    std::string result = Trim(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

bool ParseMainHotkey(const std::string& token, std::uint32_t& virtualKey) {
    if (token.size() == 1) {
        const auto c = static_cast<unsigned char>(token.front());
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            virtualKey = c;
            return true;
        }
        struct PunctuationKey {
            char character;
            std::uint32_t virtualKey;
        };
        constexpr PunctuationKey punctuationKeys[] = {
            {';', VK_OEM_1}, {'=', VK_OEM_PLUS}, {',', VK_OEM_COMMA},
            {'-', VK_OEM_MINUS}, {'.', VK_OEM_PERIOD}, {'/', VK_OEM_2},
            {'`', VK_OEM_3}, {'[', VK_OEM_4}, {'\\', VK_OEM_5},
            {']', VK_OEM_6}, {'\'', VK_OEM_7},
        };
        for (const auto& key : punctuationKeys) {
            if (c == static_cast<unsigned char>(key.character)) {
                virtualKey = key.virtualKey;
                return true;
            }
        }
    }

    if (token.size() >= 2 && token.front() == 'F') {
        unsigned value{};
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < '0' || token[index] > '9') return false;
            value = value * 10 + static_cast<unsigned>(token[index] - '0');
        }
        if (value >= 1 && value <= 24) {
            virtualKey = VK_F1 + value - 1;
            return true;
        }
    }

    struct NamedKey {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedKey namedKeys[] = {
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT},
        {"DELETE", VK_DELETE}, {"HOME", VK_HOME}, {"END", VK_END},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"ENTER", VK_RETURN}, {"BACKSPACE", VK_BACK}, {"ESCAPE", VK_ESCAPE},
        {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT},
        {"SEMICOLON", VK_OEM_1}, {"EQUALS", VK_OEM_PLUS},
        {"COMMA", VK_OEM_COMMA}, {"MINUS", VK_OEM_MINUS},
        {"PERIOD", VK_OEM_PERIOD}, {"SLASH", VK_OEM_2},
        {"BACKTICK", VK_OEM_3}, {"LBRACKET", VK_OEM_4},
        {"BACKSLASH", VK_OEM_5}, {"RBRACKET", VK_OEM_6},
        {"APOSTROPHE", VK_OEM_7},
        {"MOUSE3", VK_MBUTTON}, {"MIDDLE", VK_MBUTTON},
        {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2},
    };
    for (const auto& key : namedKeys) {
        if (token == key.name) {
            virtualKey = key.virtualKey;
            return true;
        }
    }
    return false;
}

bool ParseHotkey(std::string_view text, FloatingDamage::HotkeyBinding& output) {
    FloatingDamage::HotkeyBinding parsed{};
    parsed.virtualKey = 0;
    parsed.control = false;
    parsed.shift = false;
    parsed.alt = false;
    bool hasMainKey{};

    std::size_t begin{};
    while (begin <= text.size()) {
        const std::size_t separator = text.find('+', begin);
        const std::string token = UpperTrim(text.substr(
            begin,
            separator == std::string_view::npos ? text.size() - begin : separator - begin));
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
            if (hasMainKey || !ParseMainHotkey(token, parsed.virtualKey)) return false;
            hasMainKey = true;
        }

        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }

    if (!hasMainKey) return false;
    output = parsed;
    return true;
}

void ApplySetting(std::string_view rawKey, std::string_view value, FloatingDamage::Config& config) {
    const std::string key = Lower(Trim(rawKey));
    if (key == "enabled") ParseBool(value, config.enabled);
    else if (key == "toggle_hotkey_enabled") ParseBool(value, config.toggleHotkeyEnabled);
    else if (key == "toggle_hotkey") {
        std::string text;
        FloatingDamage::HotkeyBinding binding{};
        if (ParseTomlString(value, text) && ParseHotkey(text, binding)) {
            config.toggleHotkeyText = text;
            config.toggleHotkey = binding;
        } else {
            config.toggleHotkeyEnabled = false;
            config.toggleHotkeyText = "INVALID";
            config.toggleHotkey.virtualKey = 0;
        }
    }
    else if (key == "max_numbers_on_screen") ParseInt(value, config.maxNumbersOnScreen);
    else if (key == "font_index") ParseInt(value, config.fontIndex);
    else if (key == "color_by_damage_type") ParseBool(value, config.colorByDamageType);
    else if (key == "text_size") ParseFloat(value, config.textSize);
    else if (key == "critical_hit_size") ParseFloat(value, config.criticalHitSize);
    else if (key == "text_outline_width") ParseInt(value, config.textOutlineWidth);
    else if (key == "shadow_left_right_offset") ParseFloat(value, config.shadowLeftRightOffset);
    else if (key == "shadow_up_down_offset") ParseFloat(value, config.shadowUpDownOffset);
    else if (key == "display_time_seconds") ParseFloat(value, config.displayTimeSeconds);
    else if (key == "critical_display_time_seconds") ParseFloat(value, config.criticalDisplayTimeSeconds);
    else if (key == "fade_out_start") ParseFloat(value, config.fadeOutStart);
    else if (key == "spawn_size") ParseFloat(value, config.spawnSize);
    else if (key == "pop_bounce_size") ParseFloat(value, config.popBounceSize);
    else if (key == "pop_in_time_seconds") ParseFloat(value, config.popInTimeSeconds);
    else if (key == "settle_time_seconds") ParseFloat(value, config.settleTimeSeconds);
    else if (key == "upward_drift_speed") ParseFloat(value, config.upwardDriftSpeed);
    else if (key == "sideways_spread") ParseFloat(value, config.sidewaysSpread);
    else if (key == "spawn_height_offset") ParseFloat(value, config.spawnHeightOffset);
    else if (key == "enable_hit_combining") ParseBool(value, config.enableHitCombining);
    else if (key == "max_combined_hit_size") ParseInt(value, config.maxCombinedHitSize);
    else if (key == "combine_window_ms") ParseInt(value, config.combineWindowMs);
    else if (key == "extend_display_on_hit_seconds") ParseFloat(value, config.extendDisplayOnHitSeconds);
    else if (key == "hit_pulse_size") ParseFloat(value, config.hitPulseSize);
    else if (key == "hit_pulse_time_seconds") ParseFloat(value, config.hitPulseTimeSeconds);
    else if (key == "show_tick_popups") ParseBool(value, config.showTickPopups);
    else if (key == "tick_popup_time_seconds") ParseFloat(value, config.tickPopupTimeSeconds);
    else if (key == "tick_popup_size") ParseFloat(value, config.tickPopupSize);
    else if (key == "tick_popup_travel") ParseFloat(value, config.tickPopupTravel);
    else if (key == "tick_popup_height_offset") ParseFloat(value, config.tickPopupHeightOffset);
    else if (key == "spread_numbers_horizontally") ParseBool(value, config.spreadNumbersHorizontally);
    else if (key == "number_of_columns") ParseInt(value, config.numberOfColumns);
    else if (key == "column_spacing") ParseFloat(value, config.columnSpacing);
    else if (key == "stack_height_step") ParseFloat(value, config.stackHeightStep);
    else if (key == "column_reuse_time_seconds") ParseFloat(value, config.columnReuseTimeSeconds);
    else if (key == "max_stack_height") ParseFloat(value, config.maxStackHeight);
    else if (key == "show_dps_counter") ParseBool(value, config.showDpsCounter);
    else if (key == "horizontal_position_percent") ParseFloat(value, config.horizontalPositionPercent);
    else if (key == "vertical_position_percent") ParseFloat(value, config.verticalPositionPercent);
    else if (key == "dps_sample_time_seconds") ParseFloat(value, config.dpsSampleTimeSeconds);
    else if (key == "preview_number_count") ParseInt(value, config.previewNumberCount);
    else if (key == "preview_spread") ParseFloat(value, config.previewSpread);
    else if (key == "normal") ParseColor(value, config.normalColor);
    else if (key == "critical") ParseColor(value, config.criticalColor);
    else if (key == "physical") ParseColor(value, config.physicalColor);
    else if (key == "fire") ParseColor(value, config.fireColor);
    else if (key == "lightning") ParseColor(value, config.lightningColor);
    else if (key == "cold") ParseColor(value, config.coldColor);
    else if (key == "poison") ParseColor(value, config.poisonColor);
    else if (key == "magic") ParseColor(value, config.magicColor);
}

void ParseConfig(std::string_view text) {
    FloatingDamage::ResetToDefaults();
    auto& config = FloatingDamage::GetConfig();
    std::size_t cursor{};
    while (cursor < text.size()) {
        const std::size_t lineEnd = text.find('\n', cursor);
        std::string line = Trim(text.substr(cursor, lineEnd == std::string_view::npos ? text.size() - cursor : lineEnd - cursor));
        cursor = lineEnd == std::string_view::npos ? text.size() : lineEnd + 1;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        line = Trim(line);
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        ApplySetting(std::string_view(line).substr(0, equals), std::string_view(line).substr(equals + 1), config);
    }
    config.fontIndex = std::clamp(config.fontIndex, 0, D3D12::kFloatingDamageFontCount - 1);
    config.maxNumbersOnScreen = std::max(config.maxNumbersOnScreen, 1);
    config.numberOfColumns = std::max(config.numberOfColumns, 1);
    FloatingDamage::SetEnabled(config.enabled);
}

bool LoadConfig(const nlohmann::json& miscConfig) {
    if (!miscConfig.is_object()) {
        throw std::invalid_argument("misc must be an object");
    }
    const auto entry = miscConfig.find("floatingDamage");
    const nlohmann::json empty = nlohmann::json::object();
    const auto& section = entry == miscConfig.end() ? empty : *entry;
    if (!section.is_object()) {
        throw std::invalid_argument("misc.floatingDamage must be an object");
    }

    constexpr auto KnownKeys = std::to_array<std::string_view>({
        "enabled", "toggleHotkeyEnabled", "toggleHotkey",
        "maxNumbersOnScreen", "fontIndex", "colorByDamageType",
        "textSize", "criticalHitSize", "textOutlineWidth",
        "shadowLeftRightOffset", "shadowUpDownOffset",
        "displayTimeSeconds", "criticalDisplayTimeSeconds", "fadeOutStart",
        "spawnSize", "popBounceSize", "popInTimeSeconds", "settleTimeSeconds",
        "upwardDriftSpeed", "sidewaysSpread", "spawnHeightOffset",
        "enableHitCombining", "maxCombinedHitSize", "combineWindowMs",
        "extendDisplayOnHitSeconds", "hitPulseSize", "hitPulseTimeSeconds",
        "showTickPopups", "tickPopupTimeSeconds", "tickPopupSize",
        "tickPopupTravel", "tickPopupHeightOffset",
        "spreadNumbersHorizontally", "numberOfColumns", "columnSpacing",
        "stackHeightStep", "columnReuseTimeSeconds", "maxStackHeight",
        "showDpsCounter", "horizontalPositionPercent",
        "verticalPositionPercent", "dpsSampleTimeSeconds",
        // previewNumberCount, previewSpread and colors are checked below.
    });
    for (const auto& [key, value] : section.items()) {
        (void)value;
        const bool known = std::find(KnownKeys.begin(), KnownKeys.end(), key)
                != KnownKeys.end()
            || key == "previewNumberCount"
            || key == "previewSpread"
            || key == "colors";
        if (!known) {
            throw std::invalid_argument(
                "unknown misc.floatingDamage setting: " + key);
        }
    }

    FloatingDamage::Config parsed{};
    const auto readBool = [&](const char* key, bool& target) {
        const auto value = section.find(key);
        if (value == section.end()) return;
        if (!value->is_boolean()) {
            throw std::invalid_argument(std::string(key) + " must be a boolean");
        }
        target = value->get<bool>();
    };
    const auto readInt = [&](const char* key, int& target) {
        const auto value = section.find(key);
        if (value == section.end()) return;
        if (!value->is_number_integer()) {
            throw std::invalid_argument(std::string(key) + " must be an integer");
        }
        const auto number = value->get<std::int64_t>();
        if (number < std::numeric_limits<int>::min()
                || number > std::numeric_limits<int>::max()) {
            throw std::invalid_argument(std::string(key) + " is out of range");
        }
        target = static_cast<int>(number);
    };
    const auto readFloat = [&](const char* key, float& target) {
        const auto value = section.find(key);
        if (value == section.end()) return;
        if (!value->is_number()) {
            throw std::invalid_argument(std::string(key) + " must be a number");
        }
        const double number = value->get<double>();
        if (!std::isfinite(number)
                || number < -std::numeric_limits<float>::max()
                || number > std::numeric_limits<float>::max()) {
            throw std::invalid_argument(std::string(key) + " is out of range");
        }
        target = static_cast<float>(number);
    };

    readBool("enabled", parsed.enabled);
    readBool("toggleHotkeyEnabled", parsed.toggleHotkeyEnabled);
    if (const auto hotkey = section.find("toggleHotkey"); hotkey != section.end()) {
        if (!hotkey->is_string()) {
            throw std::invalid_argument("toggleHotkey must be a string");
        }
        parsed.toggleHotkeyText = hotkey->get<std::string>();
        if (!ParseHotkey(parsed.toggleHotkeyText, parsed.toggleHotkey)) {
            throw std::invalid_argument("toggleHotkey is invalid or unsupported");
        }
    }
    readInt("maxNumbersOnScreen", parsed.maxNumbersOnScreen);
    readInt("fontIndex", parsed.fontIndex);
    readBool("colorByDamageType", parsed.colorByDamageType);
    readFloat("textSize", parsed.textSize);
    readFloat("criticalHitSize", parsed.criticalHitSize);
    readInt("textOutlineWidth", parsed.textOutlineWidth);
    readFloat("shadowLeftRightOffset", parsed.shadowLeftRightOffset);
    readFloat("shadowUpDownOffset", parsed.shadowUpDownOffset);
    readFloat("displayTimeSeconds", parsed.displayTimeSeconds);
    readFloat("criticalDisplayTimeSeconds", parsed.criticalDisplayTimeSeconds);
    readFloat("fadeOutStart", parsed.fadeOutStart);
    readFloat("spawnSize", parsed.spawnSize);
    readFloat("popBounceSize", parsed.popBounceSize);
    readFloat("popInTimeSeconds", parsed.popInTimeSeconds);
    readFloat("settleTimeSeconds", parsed.settleTimeSeconds);
    readFloat("upwardDriftSpeed", parsed.upwardDriftSpeed);
    readFloat("sidewaysSpread", parsed.sidewaysSpread);
    readFloat("spawnHeightOffset", parsed.spawnHeightOffset);
    readBool("enableHitCombining", parsed.enableHitCombining);
    readInt("maxCombinedHitSize", parsed.maxCombinedHitSize);
    readInt("combineWindowMs", parsed.combineWindowMs);
    readFloat("extendDisplayOnHitSeconds", parsed.extendDisplayOnHitSeconds);
    readFloat("hitPulseSize", parsed.hitPulseSize);
    readFloat("hitPulseTimeSeconds", parsed.hitPulseTimeSeconds);
    readBool("showTickPopups", parsed.showTickPopups);
    readFloat("tickPopupTimeSeconds", parsed.tickPopupTimeSeconds);
    readFloat("tickPopupSize", parsed.tickPopupSize);
    readFloat("tickPopupTravel", parsed.tickPopupTravel);
    readFloat("tickPopupHeightOffset", parsed.tickPopupHeightOffset);
    readBool("spreadNumbersHorizontally", parsed.spreadNumbersHorizontally);
    readInt("numberOfColumns", parsed.numberOfColumns);
    readFloat("columnSpacing", parsed.columnSpacing);
    readFloat("stackHeightStep", parsed.stackHeightStep);
    readFloat("columnReuseTimeSeconds", parsed.columnReuseTimeSeconds);
    readFloat("maxStackHeight", parsed.maxStackHeight);
    readBool("showDpsCounter", parsed.showDpsCounter);
    readFloat("horizontalPositionPercent", parsed.horizontalPositionPercent);
    readFloat("verticalPositionPercent", parsed.verticalPositionPercent);
    readFloat("dpsSampleTimeSeconds", parsed.dpsSampleTimeSeconds);
    readInt("previewNumberCount", parsed.previewNumberCount);
    readFloat("previewSpread", parsed.previewSpread);

    if (const auto colors = section.find("colors"); colors != section.end()) {
        if (!colors->is_object()) {
            throw std::invalid_argument("colors must be an object");
        }
        const auto readColor = [&](const char* key, ImVec4& target) {
            const auto value = colors->find(key);
            if (value == colors->end()) return;
            if (!value->is_array() || value->size() != 4) {
                throw std::invalid_argument(
                    std::string("colors.") + key + " must contain four numbers");
            }
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < components.size(); ++index) {
                if (!(*value)[index].is_number()) {
                    throw std::invalid_argument(
                        std::string("colors.") + key + " must contain four numbers");
                }
                const double number = (*value)[index].get<double>();
                if (!std::isfinite(number)
                        || number < -std::numeric_limits<float>::max()
                        || number > std::numeric_limits<float>::max()) {
                    throw std::invalid_argument(
                        std::string("colors.") + key + " is out of range");
                }
                components[index] = static_cast<float>(number);
            }
            target = ImVec4(
                components[0], components[1], components[2], components[3]);
        };
        constexpr std::array<std::string_view, 8> KnownColors{
            "normal", "critical", "physical", "fire", "lightning",
            "cold", "poison", "magic",
        };
        for (const auto& [key, value] : colors->items()) {
            (void)value;
            if (std::find(KnownColors.begin(), KnownColors.end(), key)
                    == KnownColors.end()) {
                throw std::invalid_argument("unknown color setting: " + key);
            }
        }
        readColor("normal", parsed.normalColor);
        readColor("critical", parsed.criticalColor);
        readColor("physical", parsed.physicalColor);
        readColor("fire", parsed.fireColor);
        readColor("lightning", parsed.lightningColor);
        readColor("cold", parsed.coldColor);
        readColor("poison", parsed.poisonColor);
        readColor("magic", parsed.magicColor);
    }

    parsed.fontIndex = std::clamp(
        parsed.fontIndex, 0, D3D12::kFloatingDamageFontCount - 1);
    parsed.maxNumbersOnScreen = std::max(parsed.maxNumbersOnScreen, 1);
    parsed.numberOfColumns = std::max(parsed.numberOfColumns, 1);
    FloatingDamage::GetConfig() = parsed;
    FloatingDamage::SetEnabled(parsed.enabled);
    return true;
}

constexpr std::uint64_t MakeProjectionKey(
    std::uint32_t unitType,
    std::uint32_t unitId) noexcept {
    return (static_cast<std::uint64_t>(unitType) << 32) | unitId;
}

constexpr std::size_t ProjectionCacheIndex(std::uint64_t key) noexcept {
    key ^= key >> 33;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33;
    return static_cast<std::size_t>(key) & (ProjectionCacheSize - 1);
}

constexpr std::size_t ProjectionRequestIndex(std::uint64_t key) noexcept {
    key ^= key >> 33;
    key *= UINT64_C(0xc4ceb9fe1a85ec53);
    key ^= key >> 33;
    return static_cast<std::size_t>(key)
        & (ProjectionRequestCapacity - 1);
}

static_assert((ProjectionCacheSize & (ProjectionCacheSize - 1)) == 0);
static_assert(
    (ProjectionRequestCapacity & (ProjectionRequestCapacity - 1)) == 0);
static_assert(ProjectionRequestCapacity >= 320);

void RequestTargetProjection(
    std::uint32_t unitType,
    std::uint32_t unitId) noexcept {
    if (unitType != MonsterUnitType)
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t requestedUntil = now + ProjectionRequestLeaseMs;
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    const std::size_t start = ProjectionRequestIndex(key);
    for (std::size_t probe = 0;
            probe < ProjectionRequestCapacity;
            ++probe) {
        ProjectionRequestSlot& slot = ProjectionRequests[
            (start + probe) & (ProjectionRequestCapacity - 1)];
        if (slot.writing.test(std::memory_order_acquire))
            continue;
        const std::uint64_t currentKey = slot.key.load(
            std::memory_order_acquire);
        if (currentKey == key) {
            if (slot.writing.test_and_set(std::memory_order_acquire))
                continue;
            if (slot.key.load(std::memory_order_relaxed) == key) {
                const std::uint64_t lockedUntil = slot.requestedUntil.load(
                    std::memory_order_relaxed);
                slot.requestedUntil.store(
                    std::max(lockedUntil, requestedUntil),
                    std::memory_order_relaxed);
                slot.writing.clear(std::memory_order_release);
                return;
            }
            slot.writing.clear(std::memory_order_release);
            continue;
        }

        const std::uint64_t currentUntil = slot.requestedUntil.load(
            std::memory_order_acquire);
        if (currentKey != 0 && currentUntil >= now)
            continue;
        if (slot.writing.test_and_set(std::memory_order_acquire))
            continue;

        const std::uint64_t lockedKey = slot.key.load(
            std::memory_order_relaxed);
        const std::uint64_t lockedUntil = slot.requestedUntil.load(
            std::memory_order_relaxed);
        if (lockedKey == key || lockedKey == 0 || lockedUntil < now) {
            slot.requestedUntil.store(
                requestedUntil, std::memory_order_relaxed);
            slot.key.store(key, std::memory_order_release);
            slot.writing.clear(std::memory_order_release);
            return;
        }
        slot.writing.clear(std::memory_order_release);
    }

    ProjectionRequestDrops.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t PackNativePoint(const NativeScreenPoint& point) noexcept {
    const std::uint64_t x = std::bit_cast<std::uint32_t>(point.x);
    const std::uint64_t y = std::bit_cast<std::uint32_t>(point.y);
    return x | (y << 32);
}

NativeScreenPoint UnpackNativePoint(std::uint64_t packed) noexcept {
    return NativeScreenPoint{
        std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
        std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32)),
    };
}

void RefreshNativeDimensions(std::uint64_t now) noexcept {
    const std::uint64_t previous = NativeDimensionsRefreshTick.load(
        std::memory_order_relaxed);
    if (CachedNativeWidth.load(std::memory_order_relaxed) > 0
            && CachedNativeHeight.load(std::memory_order_relaxed) > 0
            && now - previous < 1000) {
        return;
    }

    std::uint64_t expected = previous;
    if (!NativeDimensionsRefreshTick.compare_exchange_strong(
            expected, now, std::memory_order_acq_rel)) {
        return;
    }

    __try {
        const std::int32_t width = GetNativeWidth ? GetNativeWidth() : 0;
        const std::int32_t height = GetNativeHeight ? GetNativeHeight() : 0;
        if (width > 0 && height > 0) {
            CachedNativeWidth.store(width, std::memory_order_release);
            CachedNativeHeight.store(height, std::memory_order_release);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CacheNativeProjection(
    UnitView* unit,
    const NativeScreenPoint& point,
    bool elevated) noexcept {
    __try {
        if (!unit
                || unit->unitType != MonsterUnitType
                || !std::isfinite(point.x)
                || !std::isfinite(point.y)) {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        const std::uint64_t key = MakeProjectionKey(unit->unitType, unit->unitId);
        ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
        if (entry.writing.test_and_set(std::memory_order_acquire))
            return;

        if (entry.key.load(std::memory_order_relaxed) != key) {
            entry.elevatedTick.store(0, std::memory_order_relaxed);
            entry.baseTick.store(0, std::memory_order_relaxed);
        }
        const std::uint64_t packed = PackNativePoint(point);
        if (elevated) {
            entry.elevatedPoint.store(packed, std::memory_order_relaxed);
            entry.elevatedTick.store(now, std::memory_order_relaxed);
        }
        else {
            entry.basePoint.store(packed, std::memory_order_relaxed);
            entry.baseTick.store(now, std::memory_order_relaxed);
        }
        entry.visible.store(true, std::memory_order_relaxed);
        entry.attemptTick.store(now, std::memory_order_relaxed);
        entry.key.store(key, std::memory_order_release);
        entry.writing.clear(std::memory_order_release);
        RefreshNativeDimensions(now);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CacheNativeProjectionFailure(
    std::uint32_t unitType,
    std::uint32_t unitId,
    std::uint64_t now) noexcept {
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
    if (entry.writing.test_and_set(std::memory_order_acquire))
        return;
    if (entry.key.load(std::memory_order_relaxed) != key) {
        entry.elevatedTick.store(0, std::memory_order_relaxed);
        entry.baseTick.store(0, std::memory_order_relaxed);
    }
    entry.visible.store(false, std::memory_order_relaxed);
    entry.attemptTick.store(now, std::memory_order_relaxed);
    entry.key.store(key, std::memory_order_release);
    entry.writing.clear(std::memory_order_release);
}

void ProjectRequestedTargets(
    void* renderContext,
    std::uint64_t now) noexcept {
    if (!renderContext || !ProjectUnitToScreen || !GetClientUnit)
        return;
    if (ProjectionSweepActive)
        return;

    std::uint64_t previous = LastProjectionSweepTick.load(
        std::memory_order_relaxed);
    if (now - previous < ProjectionSweepIntervalMs
            || !LastProjectionSweepTick.compare_exchange_strong(
                previous, now, std::memory_order_acq_rel)) {
        return;
    }

    ProjectionSweepActive = true;
    for (ProjectionRequestSlot& slot : ProjectionRequests) {
        if (slot.writing.test(std::memory_order_acquire))
            continue;
        const std::uint64_t key = slot.key.load(std::memory_order_acquire);
        const std::uint64_t requestedUntil = slot.requestedUntil.load(
            std::memory_order_acquire);
        if (key == 0 || requestedUntil < now
                || slot.writing.test(std::memory_order_acquire)
                || slot.key.load(std::memory_order_acquire) != key) {
            continue;
        }

        const std::uint32_t unitType = static_cast<std::uint32_t>(key >> 32);
        const std::uint32_t unitId = static_cast<std::uint32_t>(key);
        bool projected{};
        __try {
            UnitView* target = GetClientUnit(unitId, unitType);
            if (target) {
                NativeScreenPoint point{};
                projected = ProjectUnitToScreen(
                    renderContext, target, &point, true);
                if (projected) {
                    CacheNativeProjection(target, point, true);
                }
                else {
                    projected = ProjectUnitToScreen(
                        renderContext, target, &point, false);
                    if (projected)
                        CacheNativeProjection(target, point, false);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            projected = false;
        }

        ActiveProjectionAttempts.fetch_add(1, std::memory_order_relaxed);
        if (!projected) {
            ActiveProjectionMisses.fetch_add(1, std::memory_order_relaxed);
            CacheNativeProjectionFailure(unitType, unitId, now);
        }
    }
    ProjectionSweepActive = false;
}

void __fastcall HookUpdateCamera() noexcept {
    if (OriginalUpdateCamera)
        OriginalUpdateCamera();

    CameraFrameTicks.fetch_add(1, std::memory_order_relaxed);
    void* renderContext{};
    __try {
        auto* root = static_cast<std::uint8_t*>(
            GetRenderThreadContextRoot
                ? GetRenderThreadContextRoot()
                : nullptr);
        if (root)
            renderContext = *reinterpret_cast<void**>(root + 0x20);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        renderContext = nullptr;
    }

    if (!renderContext) {
        RenderContextMisses.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!CameraFrameReadyLogged.exchange(true, std::memory_order_acq_rel)
            && Context) {
        Context->LogInfo(
            "FloatingDamage camera-frame projection rendezvous ready on D2R's native render thread.");
    }
    ProjectRequestedTargets(renderContext, GetTickCount64());
}

bool TryReadCachedNativeProjection(
    std::uint32_t unitType,
    std::uint32_t unitId,
    NativeScreenPoint& point) noexcept {
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
    if (entry.writing.test(std::memory_order_acquire)
            || entry.key.load(std::memory_order_acquire) != key) {
        return false;
    }

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t attemptTick = entry.attemptTick.load(
        std::memory_order_relaxed);
    if (attemptTick == 0
            || now - attemptTick > ProjectionFreshnessMs
            || !entry.visible.load(std::memory_order_relaxed)) {
        return false;
    }
    const std::uint64_t elevatedTick = entry.elevatedTick.load(
        std::memory_order_relaxed);
    const std::uint64_t baseTick = entry.baseTick.load(
        std::memory_order_relaxed);
    std::uint64_t packed{};
    if (elevatedTick != 0 && now - elevatedTick <= ProjectionFreshnessMs) {
        packed = entry.elevatedPoint.load(std::memory_order_relaxed);
    }
    else if (baseTick != 0 && now - baseTick <= ProjectionFreshnessMs) {
        packed = entry.basePoint.load(std::memory_order_relaxed);
    }
    else {
        return false;
    }

    if (entry.writing.test(std::memory_order_acquire)
            || entry.key.load(std::memory_order_acquire) != key) {
        return false;
    }
    point = UnpackNativePoint(packed);
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool TryProjectTargetToScreen(
    std::uint32_t unitType,
    std::uint32_t unitId,
    float displayWidth,
    float displayHeight,
    float* screenX,
    float* screenY) noexcept {
    if (unitType != MonsterUnitType
            || !screenX
            || !screenY
            || displayWidth <= 0.0f
            || displayHeight <= 0.0f) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    RequestTargetProjection(unitType, unitId);

    NativeScreenPoint native{};
    const std::int32_t nativeWidth = CachedNativeWidth.load(
        std::memory_order_acquire);
    const std::int32_t nativeHeight = CachedNativeHeight.load(
        std::memory_order_acquire);
    if (nativeWidth <= 0
            || nativeHeight <= 0
            || !TryReadCachedNativeProjection(unitType, unitId, native)) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const float projectedX = native.x * displayWidth / static_cast<float>(nativeWidth);
    const float projectedY = native.y * displayHeight / static_cast<float>(nativeHeight);
    if (!std::isfinite(projectedX)
            || !std::isfinite(projectedY)
            || projectedX < 0.0f
            || projectedX > displayWidth
            || projectedY < 0.0f
            || projectedY > displayHeight) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    *screenX = projectedX;
    *screenY = projectedY;
    const std::uint64_t success = ProjectionSuccesses.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (success == 1
            && !ProjectionReadyLogged.exchange(true, std::memory_order_acq_rel)
            && Context) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage render-thread projection cache ready: target=%u; native=%.1f,%.1f/%dx%d; overlay=%.1f,%.1f/%.0fx%.0f.",
            unitId,
            native.x,
            native.y,
            nativeWidth,
            nativeHeight,
            projectedX,
            projectedY,
            displayWidth,
            displayHeight);
        Context->LogInfo(message);
    }
    return true;
}

bool IsCritical(void* damage) noexcept {
    __try {
        if (!damage) return false;
        const auto flags = *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(damage) + 4);
        return (flags & CriticalStrikeResultFlag) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

FloatingDamage::Element ElementFromDamage(const void* damage) noexcept {
    struct Component {
        std::size_t offset;
        FloatingDamage::Element element;
    };
    constexpr std::array components{
        Component{DamagePhysicalOffset, FloatingDamage::Element::Physical},
        Component{DamageFireOffset, FloatingDamage::Element::Fire},
        Component{DamageLightningOffset, FloatingDamage::Element::Lightning},
        Component{DamageMagicOffset, FloatingDamage::Element::Magic},
        Component{DamageColdOffset, FloatingDamage::Element::Cold},
        Component{DamagePoisonOffset, FloatingDamage::Element::Poison},
    };

    __try {
        if (!damage) return FloatingDamage::Element::Physical;
        std::int32_t largest{};
        FloatingDamage::Element result = FloatingDamage::Element::Physical;
        for (const auto& component : components) {
            const auto value = *reinterpret_cast<const std::int32_t*>(
                static_cast<const std::uint8_t*>(damage) + component.offset);
            if (value > largest) {
                largest = value;
                result = component.element;
            }
        }
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FloatingDamage::Element::Physical;
    }
}

bool TryGetMonsterId(UnitView* target, std::uint32_t& unitId) noexcept {
    __try {
        if (!target || target->unitType != MonsterUnitType) return false;
        unitId = target->unitId;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryGetFixedHitpoints(UnitView* target, std::int32_t& hitpoints) noexcept {
    __try {
        if (!target || !GetUnitStat) return false;
        hitpoints = GetUnitStat(target, HitPointsStatId, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

constexpr std::int32_t VisibleHitpoints(std::int32_t fixedHitpoints) noexcept {
    return fixedHitpoints > 0 ? fixedHitpoints >> 8 : 0;
}

constexpr std::int32_t VisibleHitpointLoss(
    std::int32_t beforeFixed,
    std::int32_t afterFixed) noexcept {
    const std::int32_t before = VisibleHitpoints(beforeFixed);
    const std::int32_t after = VisibleHitpoints(afterFixed);
    return before > after ? before - after : 0;
}

static_assert(VisibleHitpointLoss(20 * 256, 20 * 256 - 1023) == 4);
static_assert(VisibleHitpointLoss(20 * 256, 20 * 256 - 794) == 4);
static_assert(VisibleHitpointLoss(20 * 256 - 794, 20 * 256 - 1588) == 3);

__declspec(noinline) void __fastcall HookHitpointsCommit(
    UnitView* target,
    std::int32_t statId,
    std::int32_t newFixed,
    std::uint16_t layer,
    UnitView*,
    void* damage
) noexcept {
    std::uint32_t targetId{};
    std::int32_t beforeFixed{};
    const bool observe = statId == HitPointsStatId
        && layer == 0
        && TryGetMonsterId(target, targetId)
        && TryGetFixedHitpoints(target, beforeFixed);
    const bool critical = observe && IsCritical(damage);
    const FloatingDamage::Element element = observe
        ? ElementFromDamage(damage)
        : FloatingDamage::Element::Physical;

    SetUnitStat(target, statId, newFixed, layer);
    if (!observe) return;

    std::int32_t afterFixed{};
    if (!TryGetFixedHitpoints(target, afterFixed)) return;
    const std::int32_t amount = VisibleHitpointLoss(beforeFixed, afterFixed);
    if (amount <= 0) return;

    const std::uint64_t captured = CapturedEvents.fetch_add(1, std::memory_order_relaxed) + 1;
    if (captured == 1 && Context) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage captured its first committed visible HP loss: fixed=%d->%d; popup=%d.",
            beforeFixed,
            afterFixed,
            amount);
        Context->LogInfo(message);
    }
    if (!FloatingDamage::IsEnabled()) return;

    RequestTargetProjection(MonsterUnitType, targetId);
    FloatingDamage::QueueGameDamage(
        amount,
        MonsterUnitType,
        targetId,
        critical ? FloatingDamage::Kind::Critical : FloatingDamage::Kind::Normal,
        element);
    const std::uint64_t displayed = DisplayedEvents.fetch_add(1, std::memory_order_relaxed) + 1;
    if (displayed == 1 && Context) {
        Context->LogInfo("FloatingDamage queued its first committed target-monster HP loss.");
    }
}

template <std::size_t Size>
bool MatchesSignature(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected) noexcept {
    __try {
        return Base && std::memcmp(Base + rva, expected.data(), expected.size()) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* AllocateRelayPageNear(void* hint) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto base = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1);
    for (std::uintptr_t delta = granularity;
            delta < UINT64_C(0x70000000); delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta),
                systemInfo.dwPageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

bool CreateHitpointsCommitRelay() noexcept {
    HitpointsCommitRelay = AllocateRelayPageNear(Base + HitpointsCommitCallRva);
    if (!HitpointsCommitRelay) return false;

    std::array<std::uint8_t, 31> relay{
        0x48,0x83,0xEC,0x38,
        0x4C,0x89,0x74,0x24,0x20,
        0x48,0x89,0x7C,0x24,0x28,
        0x48,0xB8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0xFF,0xD0,
        0x48,0x83,0xC4,0x38,
        0xC3,
    };
    const auto hookAddress = reinterpret_cast<std::uintptr_t>(
        &HookHitpointsCommit);
    std::memcpy(relay.data() + 16, &hookAddress, sizeof(hookAddress));
    std::memcpy(HitpointsCommitRelay, relay.data(), relay.size());

    DWORD previousProtection{};
    if (!VirtualProtect(
            HitpointsCommitRelay,
            relay.size(),
            PAGE_EXECUTE_READ,
            &previousProtection)) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    FlushInstructionCache(
        GetCurrentProcess(), HitpointsCommitRelay, relay.size());

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(
        HitpointsCommitRelay);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress
            || relayAddress - baseAddress
                > std::numeric_limits<std::uint32_t>::max()) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    return true;
}

void RemoveCameraFrameHook() noexcept {
    CameraFrameHookInstalled = false;
    OriginalUpdateCamera = nullptr;
}

template <std::size_t Size>
bool InstallCameraFrameHook(
    const std::array<std::uint8_t, Size>& expected) noexcept {
    if (!PSh_ManifestInstallInlineHook(
            Context,
            PSH_MANIFEST_SITE("misc.floatingDamage.updateCamera"),
            UpdateCameraRva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()),
            &HookUpdateCamera,
            &OriginalUpdateCamera)) {
        OriginalUpdateCamera = nullptr;
        return false;
    }
    CameraFrameHookInstalled = true;
    return true;
}

bool InstallDamageHook() noexcept {
    constexpr std::array<std::uint8_t, 29> getUnitStatExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,
        0x41,0x0F,0xB7,0xE8,0x8B,0xFA,0x48,0x8B,0xD9,
    };
    constexpr std::array<std::uint8_t, 29> setUnitStatExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,
        0xEC,0x40,0x45,0x0F,0xB7,0xE1,0x45,0x8B,0xF0,
    };
    constexpr std::array<std::uint8_t, 32> getClientUnitExpected{
        0x4C,0x63,0xCA,0x48,0x8D,0x05,0x36,0x93,
        0x98,0x02,0x8B,0xD1,0x44,0x8B,0xC1,0x49,
        0x8B,0xC9,0x83,0xE2,0x7F,0x48,0xC1,0xE1,
        0x0A,0x48,0x03,0xC8,0xE9,0x7F,0x4C,0x00,
    };
    constexpr std::array<std::uint8_t, 33> updateCameraExpected{
        0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,
        0xEC,0x20,0xE8,0x31,0x17,0xFD,0xFF,0x8B,
        0xC8,0xE8,0xDA,0x08,0xFE,0xFF,0x48,0x8B,
        0xC8,0x48,0x8B,0xD8,0xE8,0xAF,0x13,0x29,
        0x00,
    };
    constexpr std::array<std::uint8_t, 37>
        getRenderThreadContextRootExpected{
        0x48,0x83,0xEC,0x28,0x65,0x48,0x8B,0x04,
        0x25,0x58,0x00,0x00,0x00,0x8B,0x0D,0x65,
        0xED,0xF2,0x02,0xBA,0x6C,0x12,0x00,0x00,
        0x48,0x8B,0x0C,0xC8,0x8B,0x04,0x0A,0x39,
        0x05,0x3F,0xF4,0xD8,0x02,
    };
    constexpr std::array<std::uint8_t, 61> projectUnitToScreenExpected{
        0x4C,0x8B,0xDC,0x55,0x56,0x57,0x41,0x57,
        0x49,0x8D,0x6B,0xA8,0x48,0x81,0xEC,0x38,
        0x01,0x00,0x00,0x48,0x8B,0x05,0xDE,0x0A,
        0x26,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,
        0xA0,0x40,0x32,0xFF,0x4C,0x89,0x44,0x24,
        0x40,0x45,0x0F,0xB6,0xF9,0x48,0x8B,0xF2,
        0x48,0x85,0xD2,0x0F,0x84,0x6E,0x07,0x00,
        0x00,0x4C,0x8B,0x82,0xD8,
    };
    constexpr std::array<std::uint8_t, 27> getNativeHeightExpected{
        0x48,0x83,0xEC,0x28,0xE8,0x67,0x6D,0x7C,
        0x00,0x84,0xC0,0x74,0x0E,0xE8,0x1E,0x51,
        0x5D,0x00,0x48,0xC1,0xE8,0x20,0x48,0x83,
        0xC4,0x28,0xC3,
    };
    constexpr std::array<std::uint8_t, 33> getNativeWidthExpected{
        0x48,0x83,0xEC,0x28,0xE8,0xF7,0x6C,0x7C,
        0x00,0x84,0xC0,0x74,0x09,0x48,0x83,0xC4,
        0x28,0xE9,0xAA,0x50,0x5D,0x00,0x8B,0x05,
        0x3C,0x18,0x22,0x02,0x48,0x83,0xC4,0x28,
        0xC3,
    };
    constexpr std::array<std::uint8_t, 21> hitpointsCommitContextExpected{
        0x48,0x8B,0xCE,0x3D,0x00,0x01,0x00,0x00,0x44,0x0F,
        0x4D,0xC0,0x41,0x8D,0x51,0x06,0xE8,0x78,0xAC,0xEA,0xFF,
    };
    constexpr std::array<std::uint8_t, 5> hitpointsCommitCallExpected{
        0xE8,0x78,0xAC,0xEA,0xFF,
    };
    if (!MatchesSignature(GetUnitStatRva, getUnitStatExpected)
            || !MatchesSignature(SetUnitStatRva, setUnitStatExpected)
            || !MatchesSignature(
                GetClientUnitRva,
                getClientUnitExpected)
            || !MatchesSignature(
                UpdateCameraRva,
                updateCameraExpected)
            || !MatchesSignature(
                GetRenderThreadContextRootRva,
                getRenderThreadContextRootExpected)
            || !MatchesSignature(
                ProjectUnitToScreenRva,
                projectUnitToScreenExpected)
            || !MatchesSignature(
                GetNativeHeightRva,
                getNativeHeightExpected)
            || !MatchesSignature(
                GetNativeWidthRva,
                getNativeWidthExpected)
            || !MatchesSignature(
                HitpointsCommitContextRva,
                hitpointsCommitContextExpected)) {
        return false;
    }
    GetUnitStat = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    SetUnitStat = reinterpret_cast<SetUnitStatFn>(Base + SetUnitStatRva);
    GetClientUnit = reinterpret_cast<GetClientUnitFn>(
        Base + GetClientUnitRva);
    ProjectUnitToScreen = reinterpret_cast<ProjectUnitToScreenFn>(
        Base + ProjectUnitToScreenRva);
    GetRenderThreadContextRoot =
        reinterpret_cast<GetRenderThreadContextRootFn>(
            Base + GetRenderThreadContextRootRva);
    GetNativeHeight = reinterpret_cast<GetNativeDimensionFn>(
        Base + GetNativeHeightRva);
    GetNativeWidth = reinterpret_cast<GetNativeDimensionFn>(
        Base + GetNativeWidthRva);
    if (!InstallCameraFrameHook(updateCameraExpected)) return false;
    if (!CreateHitpointsCommitRelay()) {
        RemoveCameraFrameHook();
        return false;
    }
    const auto relayRva = reinterpret_cast<std::uintptr_t>(
        HitpointsCommitRelay) - reinterpret_cast<std::uintptr_t>(Base);
    if (!PSh_ManifestPatchCallRel32(
            Context,
            PSH_MANIFEST_SITE("misc.floatingDamage.hitpointsCommitCall"),
            HitpointsCommitCallRva,
            hitpointsCommitCallExpected.data(),
            static_cast<std::uint32_t>(hitpointsCommitCallExpected.size()),
            relayRva,
            5)) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        RemoveCameraFrameHook();
        return false;
    }
    return true;
}

DWORD WINAPI OverlayWorkerMain(void*) noexcept {
    const HMODULE pluginItems = GetModuleHandleW(L"plugin-items.dll");
    const bool embeddedExtendedItemStats = pluginItems
        && GetProcAddress(
            pluginItems,
            "ExtendedItemStatsOwnsTooltipPipeline")
        && GetProcAddress(
            pluginItems,
            "ExtendedItemStatsTransformTooltip");
    if (embeddedExtendedItemStats) {
        if (Context) {
            Context->LogInfo(
                "FloatingDamage overlay: plugin-items embeds ExtendedItemStats; deferring D3D12 hook ownership so its fallback renderer can install first.");
        }
        if (WaitForSingleObject(OverlayStopEvent, 5000) != WAIT_TIMEOUT)
            return 0;
    }
    while (WaitForSingleObject(OverlayStopEvent, 500) == WAIT_TIMEOUT) {
        if (!D3D12::InstallHooks()) continue;
        OverlayReady.store(true, std::memory_order_release);
        if (Context) {
            Context->LogInfo(
                embeddedExtendedItemStats
                    ? "FloatingDamage: DirectX 12 overlay hooks installed after plugin-items for deterministic renderer chaining."
                    : "FloatingDamage: DirectX 12 overlay hooks installed after graphics startup.");
        }
        return 0;
    }
    return 0;
}

bool StartOverlayWorker() noexcept {
    OverlayStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!OverlayStopEvent) return false;
    OverlayWorker = CreateThread(nullptr, 0, OverlayWorkerMain, nullptr, 0, nullptr);
    if (OverlayWorker) return true;
    CloseHandle(OverlayStopEvent);
    OverlayStopEvent = nullptr;
    return false;
}

auto ConsoleCommand(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    const std::string action = Lower(Trim(command->args ? std::string_view(command->args, command->argsLength) : std::string_view{}));
    auto& config = FloatingDamage::GetConfig();
    const bool enabled = FloatingDamage::IsEnabled();

    if (action.empty() || action == "status") {
        char message[768]{};
        float displayWidth{};
        float displayHeight{};
        D3D12::GetDisplaySize(displayWidth, displayHeight);
        const D3D12::OverlayDiagnostics overlay =
            D3D12::GetOverlayDiagnostics();
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage 1.2.9: enabled=%s; hotkey=%s (%s); overlay_hooks=%s; presents=%llu; queues=%llu; imgui_attempts=%llu; imgui_failures=%llu; init_stage=%u; overlay_frames=%llu; camera_frames=%llu; context_misses=%llu; captured=%llu; queued=%llu; projected=%llu; rejected=%llu; forced=%llu; missed=%llu; request_drops=%llu; active=%zu; pending=%zu; font=%d; display=%.0fx%.0f; scale=%.3f.",
            enabled ? "true" : "false",
            config.toggleHotkeyText.c_str(),
            config.toggleHotkeyEnabled ? "enabled" : "disabled",
            OverlayReady.load(std::memory_order_acquire) ? "ready" : "waiting",
            static_cast<unsigned long long>(overlay.presentCalls),
            static_cast<unsigned long long>(overlay.directQueueCaptures),
            static_cast<unsigned long long>(overlay.rendererInitAttempts),
            static_cast<unsigned long long>(overlay.rendererInitFailures),
            overlay.lastInitFailureStage,
            static_cast<unsigned long long>(overlay.renderedFrames),
            static_cast<unsigned long long>(CameraFrameTicks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(RenderContextMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(CapturedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(DisplayedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionSuccesses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionFailures.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionRequestDrops.load(std::memory_order_relaxed)),
            FloatingDamage::ActiveCount(),
            FloatingDamage::PendingCount(),
            config.fontIndex,
            displayWidth,
            displayHeight,
            FloatingDamage::GetResolutionScale(displayHeight));
        command->plugin->WriteConsoleMessage(message);
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "on" || action == "off" || action == "toggle") {
        FloatingDamage::SetEnabled(action == "toggle" ? !enabled : action == "on");
        const bool nowEnabled = FloatingDamage::IsEnabled();
        command->plugin->WriteConsoleMessage(
            nowEnabled
                ? "Floating Damage enabled for this session."
                : "Floating Damage disabled for this session.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "preview") {
        float width{}, height{};
        D3D12::GetDisplaySize(width, height);
        FloatingDamage::QueuePreviewBurstAt(width * 0.5f, height * 0.5f);
        command->plugin->WriteConsoleMessage("Floating Damage preview queued.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "reload") {
        try {
            const auto root = PSh_Json_LoadConfig(Context);
            const auto misc = PSh_Json_GetSection(root, "misc");
            if (!LoadConfig(misc)) return D2RL::ConsoleCommandResult::Failed;
        }
        catch (const std::exception& error) {
            PSh_Json_LogConfigError(Context, error);
            return D2RL::ConsoleCommandResult::Failed;
        }
        command->plugin->WriteConsoleMessage("Floating Damage configuration reloaded.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "reset") {
        FloatingDamage::GetConfig() = FloatingDamage::Config{};
        FloatingDamage::SetEnabled(false);
        command->plugin->WriteConsoleMessage(
            "Floating Damage defaults restored for this session.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    command->plugin->WriteConsoleMessage("Usage: floating-damage [status|on|off|toggle|preview|reload|reset].");
    return D2RL::ConsoleCommandResult::InvalidArguments;
}
} // namespace

extern "C" __declspec(dllexport) bool __cdecl
FloatingDamageRegisterExternalOverlay(
    D3D12::ExternalOverlayCallback callback) noexcept {
    D3D12::SetExternalOverlayCallback(callback);
    return true;
}

extern "C" __declspec(dllexport) bool __cdecl
FloatingDamageRegisterNamedExternalOverlay(
    const char* owner,
    D3D12::ExternalOverlayCallback callback) noexcept {
    return D3D12::RegisterNamedExternalOverlay(owner, callback);
}

extern "C" __declspec(dllexport) void __cdecl
FloatingDamageOverlayAddRect(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha,
    float thickness) noexcept {
    D3D12::OverlayAddRect(
        drawList, left, top, right, bottom,
        red, green, blue, alpha, thickness);
}

extern "C" __declspec(dllexport) void __cdecl
FloatingDamageOverlayAddRectFilled(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha) noexcept {
    D3D12::OverlayAddRectFilled(
        drawList, left, top, right, bottom,
        red, green, blue, alpha);
}

extern "C" __declspec(dllexport) void __cdecl
FloatingDamageOverlayAddTooltip(
    void* drawList,
    float x,
    float y,
    float displayWidth,
    float displayHeight,
    const char* text) noexcept {
    D3D12::OverlayAddTooltip(
        drawList, x, y, displayWidth, displayHeight, text);
}

namespace RuffnecKk::FloatingDamageFeature {

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Load),
        &Module);
    if (!Base || !Module) return false;
    if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("FloatingDamage: only D2R build 92777 is supported.");
        return false;
    }
    try {
        if (!LoadConfig(miscConfig)) return false;
    }
    catch (const std::exception& error) {
        const std::string message = std::string(
            "plugin-misc: invalid misc.floatingDamage (")
            + error.what() + ").";
        context->LogError(message.c_str());
        return false;
    }
    if (!InstallDamageHook()) {
        context->LogError("FloatingDamage: D2R 3.2.92777 HP commit, client-unit lookup, camera-frame or native projection signatures could not be installed; plugin refused.");
        return false;
    }
    D3D12::SetDllModule(Module);
    D3D12::SetDiagnosticLogCallback(LogOverlayDiagnostic);
    FloatingDamage::SetTargetScreenPositionProvider(TryProjectTargetToScreen);
    if (!StartOverlayWorker()) {
        FloatingDamage::SetTargetScreenPositionProvider(nullptr);
        RemoveCameraFrameHook();
        context->LogError("FloatingDamage: DirectX 12 overlay worker could not be started.");
        return false;
    }

    if (!PSh_RegisterConsoleCommand(
            context,
            "floating-damage",
            ConsoleCommand,
            "Control Floating Damage and show its status.")) {
        context->LogWarn("FloatingDamage: console command could not be registered.");
    }
    context->LogInfo("FloatingDamage 1.2.9 active for D2R 3.2.92777 with overlay render-path diagnostics and per-frame camera-thread multi-target projection.");
    return true;
}

void Unload() noexcept {
    FloatingDamage::RemoveToggleHotkeyHook();
    FloatingDamage::SetTargetScreenPositionProvider(nullptr);
    D3D12::SetExternalOverlayCallback(nullptr);
    D3D12::ClearNamedExternalOverlays();
    if (Context) {
        char message[320]{};
        const D3D12::OverlayDiagnostics overlay =
            D3D12::GetOverlayDiagnostics();
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage stopped: presents=%llu; queues=%llu; imgui_attempts=%llu; imgui_failures=%llu; init_stage=%u; overlay_frames=%llu; camera_frames=%llu; context_misses=%llu; captured=%llu; queued=%llu; forced=%llu; missed=%llu; request_drops=%llu.",
            static_cast<unsigned long long>(overlay.presentCalls),
            static_cast<unsigned long long>(overlay.directQueueCaptures),
            static_cast<unsigned long long>(overlay.rendererInitAttempts),
            static_cast<unsigned long long>(overlay.rendererInitFailures),
            overlay.lastInitFailureStage,
            static_cast<unsigned long long>(overlay.renderedFrames),
            static_cast<unsigned long long>(CameraFrameTicks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(RenderContextMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(CapturedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(DisplayedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionRequestDrops.load(std::memory_order_relaxed)));
        Context->LogInfo(message);
    }
    if (OverlayStopEvent) SetEvent(OverlayStopEvent);
    if (OverlayWorker) {
        WaitForSingleObject(OverlayWorker, 3000);
        CloseHandle(OverlayWorker);
        OverlayWorker = nullptr;
    }
    if (OverlayStopEvent) {
        CloseHandle(OverlayStopEvent);
        OverlayStopEvent = nullptr;
    }
    D3D12::RemoveHooks();
    D3D12::SetDiagnosticLogCallback(nullptr);
    RemoveCameraFrameHook();
    if (HitpointsCommitRelay) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
    }
    OverlayReady.store(false, std::memory_order_release);
    Module = nullptr;
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::FloatingDamageFeature
