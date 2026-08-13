#include "socket_tooltip.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string>
#include <utility>

namespace tcp::tooltips {
namespace {

constexpr char ColorMarker[] = "\xEE\x81\xBE";

struct LocaleLabels {
    std::string_view defenseFingerprint;
    std::string_view maxSocketsFormat;
    std::string_view baseDefenseFormat;
};

// D2R's thirteen shipped locales. Only labels invented by this plugin live in
// this table; every native stat description is resolved from the active
// language database and therefore follows mod-provided translations too.
constexpr std::array LocaleLabelTable{
    LocaleLabels{"Defense: %d", "Max Sockets: %d", "Base Defense: %d"},
    LocaleLabels{"é˜²ç¦¦ï¼š%d", "æœ€å¤§é‘²å­”æ•¸ï¼š%d", "åŸºç¤Žé˜²ç¦¦ï¼š%d"},
    LocaleLabels{"Verteidigung: %d", "Maximale Sockel: %d", "Grundverteidigung: %d"},
    LocaleLabels{"Defensa: %d", "Engarces mÃ¡ximos: %d", "Defensa base: %d"},
    LocaleLabels{"DÃ©fenseÂ : %d", "ChÃ¢sses maximales : %d", "DÃ©fense de base : %d"},
    LocaleLabels{"Difesa: %d", "Castoni massimi: %d", "Difesa base: %d"},
    LocaleLabels{"ë°©ì–´ë ¥: %d", "ìµœëŒ€ í™ˆ: %d", "ê¸°ë³¸ ë°©ì–´ë ¥: %d"},
    LocaleLabels{"Obrona: %d", "Maksymalna liczba gniazd: %d", "Bazowa obrona: %d"},
    // esMX shares the same built-in labels as esES.
    LocaleLabels{"Defensa: %d", "Engarces mÃ¡ximos: %d", "Defensa base: %d"},
    LocaleLabels{"é˜²å¾¡åŠ›: %d", "æœ€å¤§ã‚½ã‚±ãƒƒãƒˆæ•°: %d", "åŸºæœ¬é˜²å¾¡åŠ›: %d"},
    LocaleLabels{"Defesa: %d", "Soquetes mÃ¡ximos: %d", "Defesa base: %d"},
    LocaleLabels{"Ð—Ð°Ñ‰Ð¸Ñ‚Ð°: %d", "ÐœÐ°ÐºÑÐ¸Ð¼ÑƒÐ¼ Ð³Ð½ÐµÐ·Ð´: %d", "Ð‘Ð°Ð·Ð¾Ð²Ð°Ñ Ð·Ð°Ñ‰Ð¸Ñ‚Ð°: %d"},
    LocaleLabels{"é˜²å¾¡: %d", "æœ€å¤§é•¶å­”æ•°ï¼š%d", "åŸºç¡€é˜²å¾¡ï¼š%d"},
};

enum class PlaceholderKind { SignedInteger, UnsignedInteger, Text };

struct FormatToken {
    std::string literal;
    PlaceholderKind placeholder{PlaceholderKind::Text};
    bool hasPlaceholder{};
};

std::string StripColors(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xEE
            && static_cast<unsigned char>(text[index + 1]) == 0x81
            && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
            index += 4;
        } else if (index + 2 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xFF
            && text[index + 1] == 'c') {
            index += 3;
        } else if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xC3
            && static_cast<unsigned char>(text[index + 1]) == 0xBF
            && text[index + 2] == 'c') {
            index += 4;
        } else {
            clean.push_back(text[index++]);
        }
    }
    return clean;
}

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    std::size_t first{};
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    return value.substr(first);
}

std::vector<FormatToken> TokenizeFormat(std::string_view rawFormat) {
    const auto format = StripColors(rawFormat);
    std::vector<FormatToken> tokens;
    std::string literal;
    for (std::size_t index = 0; index < format.size();) {
        if (format[index] != '%') {
            literal.push_back(format[index++]);
            continue;
        }
        if (index + 1 < format.size() && format[index + 1] == '%') {
            literal.push_back('%');
            index += 2;
            continue;
        }

        const auto placeholderStart = index++;
        bool explicitSign{};
        while (index < format.size()
            && (format[index] == '+' || format[index] == '-' || format[index] == ' '
                || format[index] == '#')) {
            explicitSign = explicitSign || format[index] == '+';
            ++index;
        }
        const auto digitsStart = index;
        while (index < format.size()
            && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
        if (index < format.size() && format[index] == '.') {
            ++index;
            while (index < format.size()
                && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
        }
        while (index < format.size()
            && (format[index] == 'h' || format[index] == 'l'
                || format[index] == 'j' || format[index] == 'z'
                || format[index] == 't' || format[index] == 'L')) ++index;

        PlaceholderKind kind = PlaceholderKind::Text;
        bool valid{};
        if (index < format.size()) {
            const auto specifier = format[index];
            if (specifier == 'd' || specifier == 'i') {
                kind = PlaceholderKind::SignedInteger;
                valid = true;
            } else if (specifier == 'u' || specifier == 'o'
                || specifier == 'x' || specifier == 'X') {
                kind = PlaceholderKind::UnsignedInteger;
                valid = true;
            } else if (specifier == 's' || specifier == 'c'
                || specifier == 'f' || specifier == 'F'
                || specifier == 'e' || specifier == 'E'
                || specifier == 'g' || specifier == 'G') {
                kind = PlaceholderKind::Text;
                valid = true;
            }
            if (valid) ++index;
        }
        // D2R also uses positional placeholders such as %0, %1 and %+0.
        if (!valid && digitsStart < index) {
            kind = explicitSign ? PlaceholderKind::SignedInteger : PlaceholderKind::Text;
            valid = true;
        }
        if (!valid) {
            literal.append(format.substr(placeholderStart, index - placeholderStart));
            continue;
        }
        tokens.push_back({std::move(literal), kind, true});
        literal.clear();
    }
    tokens.push_back({std::move(literal), PlaceholderKind::Text, false});
    return tokens;
}

bool ConsumeInteger(std::string_view line, std::size_t& position, bool allowSign) {
    const auto start = position;
    if (position < line.size() && (line[position] == '+' || line[position] == '-')) {
        if (!allowSign) return false;
        ++position;
    }
    const auto digits = position;
    while (position < line.size()
        && std::isdigit(static_cast<unsigned char>(line[position]))) ++position;
    if (position == digits) {
        position = start;
        return false;
    }
    return true;
}

void PushResolved(std::vector<std::string>& target, std::string value) {
    if (value.empty()) return;
    if (std::find(target.begin(), target.end(), value) == target.end())
        target.push_back(std::move(value));
}

std::string Resolve(const LocalizedStringResolver& resolver, std::string_view key) {
    if (!resolver || key.empty()) return {};
    auto value = resolver(key);
    if (value.empty() || value == key) return {};
    return value;
}

bool IsPrimaryItemStatLine(std::string_view line, const TooltipLocalization& localization) {
    return std::any_of(localization.primaryStatTemplates.begin(),
        localization.primaryStatTemplates.end(), [&](const auto& format) {
            return MatchesLocalizedTemplate(line, format, true);
        });
}

} // namespace

TooltipLocalization BuildTooltipLocalization(
    const std::unordered_map<std::string, std::vector<std::string>>& statStringKeys,
    const LocalizedStringResolver& resolver) {
    TooltipLocalization result;
    for (const auto& [stat, keys] : statStringKeys) {
        auto& templates = result.statTemplates[stat];
        for (const auto& key : keys) PushResolved(templates, Resolve(resolver, key));
    }

    const auto addStatTemplate = [&](std::string_view stat, std::string_view key) {
        PushResolved(result.statTemplates[std::string(stat)], Resolve(resolver, key));
    };
    const auto addCompoundTemplate = [&](std::string_view stat, std::string_view key) {
        const auto resolved = Resolve(resolver, key);
        PushResolved(result.statTemplates[std::string(stat)], resolved);
        PushResolved(result.compoundDamageTemplates[std::string(stat)], resolved);
    };
    // D2R collapses matching minimum/maximum damage stats into one rendered
    // line that is not named by itemstatcost.txt. Associate the native range
    // formats with both components so their two rolls remain independent.
    for (const auto stat : {"mindamage", "maxdamage", "secondary_mindamage",
            "secondary_maxdamage"}) addCompoundTemplate(stat, "strModMinDamageRange");
    for (const auto stat : {"firemindam", "firemaxdam"})
        addCompoundTemplate(stat, "strModFireDamageRange");
    for (const auto stat : {"lightmindam", "lightmaxdam"})
        addCompoundTemplate(stat, "strModLightningDamageRange");
    for (const auto stat : {"magicmindam", "magicmaxdam"})
        addCompoundTemplate(stat, "strModMagicDamageRange");
    for (const auto stat : {"coldmindam", "coldmaxdam"})
        addCompoundTemplate(stat, "strModColdDamageRange");
    addStatTemplate("enhanced_damage", "strModEnhancedDamage");
    // Skill-tab descriptions are a consecutive localization family selected
    // by the property parameter rather than a single generic format.
    for (int index = 1; index <= 21; ++index)
        addStatTemplate("item_addskill_tab", "StrSklTabItem" + std::to_string(index));

    constexpr std::array primaryKeys{
        "ItemStats1g", "ItemStats1h", "ItemStats1l", "ItemStats1m",
        "ItemStats1n", "ItemStats1o", "ModStre10k", "strItemStatThrowDamageRange"};
    for (const auto key : primaryKeys)
        PushResolved(result.primaryStatTemplates, Resolve(resolver, key));
    PushResolved(result.defenseTemplates, Resolve(resolver, "ItemStats1h"));

    constexpr std::array metadataKeys{
        "ItemStats1d", "ItemStats1e", "ItemStats1f", "ItemStats1h",
        "ItemStats1i", "ItemStats1l", "ItemStats1m", "ItemStats1n",
        "ItemStats1o", "ItemStats1p", "ItemStats1r", "Socketable", "cost"};
    for (const auto key : metadataKeys)
        PushResolved(result.metadataTemplates, Resolve(resolver, key));
    for (const auto& format : result.primaryStatTemplates)
        PushResolved(result.metadataTemplates, format);

    const auto defense = Resolve(resolver, "ItemStats1h");
    result.nativeReady = !defense.empty();
    if (const auto locale = std::find_if(LocaleLabelTable.begin(), LocaleLabelTable.end(),
            [&](const auto& candidate) { return candidate.defenseFingerprint == defense; });
        locale != LocaleLabelTable.end()) {
        result.maxSocketsFormat = locale->maxSocketsFormat;
        result.baseDefenseFormat = locale->baseDefenseFormat;
    }
    if (const auto separator = Resolve(resolver, "ItemStast1k"); !separator.empty())
        result.rangeSeparator = separator;
    PushResolved(result.metadataTemplates, result.maxSocketsFormat);
    PushResolved(result.metadataTemplates, result.baseDefenseFormat);
    return result;
}

bool MatchesLocalizedTemplate(
    std::string_view rawLine,
    std::string_view rawFormat,
    bool allowTrailingText) {
    const auto line = Trim(StripColors(rawLine));
    const auto tokens = TokenizeFormat(Trim(StripColors(rawFormat)));
    if (line.empty() || tokens.empty()) return false;
    bool hasLiteral{};
    std::size_t position{};
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        if (!token.literal.empty()) {
            hasLiteral = true;
            if (line.substr(position, token.literal.size()) != token.literal) return false;
            position += token.literal.size();
        }
        if (!token.hasPlaceholder) continue;

        if (token.placeholder == PlaceholderKind::SignedInteger) {
            if (!ConsumeInteger(line, position, true)) return false;
        } else if (token.placeholder == PlaceholderKind::UnsignedInteger) {
            if (!ConsumeInteger(line, position, false)) return false;
        } else {
            const auto& nextLiteral = tokens[index + 1].literal;
            if (nextLiteral.empty()) {
                position = line.size();
            } else {
                const auto found = line.find(nextLiteral, position);
                if (found == std::string::npos || found == position) return false;
                position = found;
            }
        }
    }
    if (!hasLiteral) return false;
    const auto trailing = Trim(line.substr(position));
    return trailing.empty() || trailing.starts_with('[') || allowTrailingText;
}

std::string FormatLocalizedInteger(std::string_view format, std::int32_t value) {
    std::string result;
    result.reserve(format.size() + 16);
    bool replaced{};
    for (std::size_t index = 0; index < format.size();) {
        if (format[index] != '%') {
            result.push_back(format[index++]);
            continue;
        }
        if (index + 1 < format.size() && format[index + 1] == '%') {
            result.push_back('%');
            index += 2;
            continue;
        }
        const auto start = index++;
        while (index < format.size()
            && (format[index] == '+' || format[index] == '-' || format[index] == ' '
                || format[index] == '#')) ++index;
        const auto digitsStart = index;
        while (index < format.size()
            && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
        while (index < format.size()
            && (format[index] == 'h' || format[index] == 'l'
                || format[index] == 'j' || format[index] == 'z'
                || format[index] == 't')) ++index;
        bool placeholder = digitsStart < index;
        if (index < format.size()
            && (format[index] == 'd' || format[index] == 'i' || format[index] == 'u')) {
            placeholder = true;
            ++index;
        }
        if (!replaced && placeholder) {
            result += std::to_string(value);
            replaced = true;
        } else {
            result.append(format.substr(start, index - start));
        }
    }
    if (!replaced) result += std::to_string(value);
    return result;
}

std::string FormatMaxSocketsLine(unsigned maximumSockets, int currentSockets) {
    return FormatMaxSocketsLine(maximumSockets, currentSockets, TooltipLocalization{});
}

std::string FormatMaxSocketsLine(
    unsigned maximumSockets,
    int,
    const TooltipLocalization& localization) {
    if (maximumSockets == 0) return {};
    std::string result(ColorMarker, 3);
    result += '0';
    result += FormatLocalizedInteger(
        localization.maxSocketsFormat, static_cast<std::int32_t>(maximumSockets));
    return result;
}

std::size_t FindMaxSocketsInsertion(std::string_view tooltip) {
    TooltipLocalization english;
    english.primaryStatTemplates = {
        "Damage:", "Defense: %d", "One-Hand Damage: %d to %d",
        "Two-Hand Damage: %d to %d", "Throw Damage: %d to %d",
        "Smite Damage: %d to %d", "Kick Damage: %d to %d"};
    return FindMaxSocketsInsertion(tooltip, english);
}

std::size_t FindMaxSocketsInsertion(
    std::string_view tooltip,
    const TooltipLocalization& localization) {
    std::size_t start{};
    while (start < tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        const auto lineEnd = end == std::string_view::npos ? tooltip.size() : end;
        if (IsPrimaryItemStatLine(tooltip.substr(start, lineEnd - start), localization))
            return start;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return NoSocketLineInsertion;
}

} // namespace tcp::tooltips
