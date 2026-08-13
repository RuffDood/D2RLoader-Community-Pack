#include "tooltip_ranges.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>
#include <unordered_set>

namespace tcp::tooltips {
namespace {

using Row = std::unordered_map<std::string, std::string>;
constexpr char ColorMarker[] = "\xEE\x81\xBE";

struct ColorCode {
    char value{};
    std::size_t length{};
};

std::optional<ColorCode> ReadColorCode(std::string_view text, std::size_t index) {
    if (index + 3 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xEE
        && static_cast<unsigned char>(text[index + 1]) == 0x81
        && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
        return ColorCode{text[index + 3], 4};
    }
    if (index + 2 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xFF
        && text[index + 1] == 'c') {
        return ColorCode{text[index + 2], 3};
    }
    if (index + 3 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xC3
        && static_cast<unsigned char>(text[index + 1]) == 0xBF
        && text[index + 2] == 'c') {
        return ColorCode{text[index + 3], 4};
    }
    return std::nullopt;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    std::size_t first{};
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    return value.substr(first);
}

std::vector<std::string> SplitTabs(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> fields;
    std::size_t start{};
    while (true) {
        const auto tab = line.find('\t', start);
        fields.emplace_back(line.substr(start, tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

bool ParseTsv(std::string_view name, std::string_view text,
    std::vector<Row>& rows, std::string& error) {
    std::istringstream input{std::string{text}};
    std::string line;

    if (!std::getline(input, line)) {
        error = "Missing header in " + std::string(name);
        return false;
    }
    auto headers = SplitTabs(std::move(line));
    for (auto& header : headers) header = Lower(Trim(std::move(header)));
    while (std::getline(input, line)) {
        const auto values = SplitTabs(std::move(line));
        Row row;
        for (std::size_t index = 0; index < headers.size() && index < values.size(); ++index) {
            if (!values[index].empty()) row.emplace(headers[index], values[index]);
        }
        rows.emplace_back(std::move(row));
    }
    return true;
}

std::string Get(const Row& row, std::string_view key) {
    const auto found = row.find(std::string(key));
    return found == row.end() ? std::string{} : found->second;
}

std::int32_t Number(const Row& row, std::string_view key) {
    const auto value = Get(row, key);
    std::int32_t result{};
    if (value.empty()) return result;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} ? result : 0;
}

std::string StripColors(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (const auto color = ReadColorCode(text, index)) {
            index += color->length;
        } else {
            clean.push_back(text[index++]);
        }
    }
    return clean;
}

std::string StripTrailingRangeAnnotation(std::string_view text) {
    auto clean = Trim(StripColors(text));
    if (!clean.ends_with(']')) return clean;
    const auto opening = clean.rfind(" [");
    if (opening == std::string::npos) return clean;
    return Trim(clean.substr(0, opening));
}

char LastColor(std::string_view line, char inheritedColor) {
    char result = inheritedColor;
    for (std::size_t index = 0; index < line.size();) {
        if (const auto color = ReadColorCode(line, index)) {
            result = color->value;
            index += color->length;
        } else {
            ++index;
        }
    }
    return result;
}

std::string NormalizeWords(std::string_view text) {
    const auto clean = StripColors(text);
    std::string normalized;
    bool space{};
    for (const auto raw : clean) {
        const auto ch = static_cast<unsigned char>(raw);
        if (std::isalpha(ch) || ch >= 0x80) {
            if (space && !normalized.empty()) normalized.push_back(' ');
            normalized.push_back(ch < 0x80 ? static_cast<char>(std::tolower(ch)) : raw);
            space = false;
        } else if (!normalized.empty()) {
            space = true;
        }
    }
    return normalized;
}


std::string NormalizeAnchor(std::string_view text) {
    std::string withoutPlaceholders;
    withoutPlaceholders.reserve(text.size());
    bool placeholder{};
    for (const auto ch : text) {
        if (ch == '[') placeholder = true;
        else if (ch == ']') placeholder = false;
        else if (!placeholder) withoutPlaceholders.push_back(ch);
    }
    return NormalizeWords(withoutPlaceholders);
}

std::vector<std::int32_t> SignedIntegers(std::string_view text) {
    const auto clean = StripColors(text);
    std::vector<std::int32_t> values;
    for (std::size_t index = 0; index < clean.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(clean[index]))
            && !((clean[index] == '+' || clean[index] == '-') && index + 1 < clean.size()
                && std::isdigit(static_cast<unsigned char>(clean[index + 1])))) continue;
        const auto start = index;
        if (clean[index] == '+' || clean[index] == '-') ++index;
        while (index < clean.size() && std::isdigit(static_cast<unsigned char>(clean[index]))) ++index;
        std::int32_t value{};
        const auto parseStart = clean[start] == '+' ? start + 1 : start;
        const auto parsed = std::from_chars(clean.data() + parseStart, clean.data() + index, value);
        if (parsed.ec == std::errc{}) values.push_back(value);
        if (index) --index;
    }
    return values;
}

std::string_view RangeStatKey(const ModifierRange& range) {
    if (!range.statKey.empty()) return range.statKey;
    const auto separator = range.key.find(':');
    return std::string_view(range.key).substr(0, separator);
}

std::optional<std::size_t> CompoundDamageSlot(std::string_view line,
    const ModifierRange& range, const TooltipLocalization* localization) {
    const auto key = RangeStatKey(range);
    if (localization) {
        if (const auto found = localization->compoundDamageTemplates.find(std::string(key));
            found != localization->compoundDamageTemplates.end()
            && std::any_of(found->second.begin(), found->second.end(),
                [&](const auto& format) {
                    return MatchesLocalizedTemplate(line, format);
                })) {
            if (key == "mindamage" || key == "secondary_mindamage"
                || key == "firemindam" || key == "lightmindam"
                || key == "magicmindam" || key == "coldmindam") return 0;
            if (key == "maxdamage" || key == "secondary_maxdamage"
                || key == "firemaxdam" || key == "lightmaxdam"
                || key == "magicmaxdam" || key == "coldmaxdam") return 1;
        }
    }
    const auto normalizedLine = NormalizeWords(line);
    if (!normalizedLine.starts_with("adds ") || !normalizedLine.ends_with(" damage"))
        return std::nullopt;
    constexpr std::array components{
        std::array<std::string_view, 3>{"mindamage", "maxdamage", " damage"},
        std::array<std::string_view, 3>{"firemindam", "firemaxdam", "fire damage"},
        std::array<std::string_view, 3>{"lightmindam", "lightmaxdam", "lightning damage"},
        std::array<std::string_view, 3>{"magicmindam", "magicmaxdam", "magic damage"},
        std::array<std::string_view, 3>{"coldmindam", "coldmaxdam", "cold damage"}
    };
    for (const auto& component : components) {
        if (normalizedLine.find(component[2]) == std::string_view::npos) continue;
        if (key == component[0]) return 0;
        if (key == component[1]) return 1;
    }
    return std::nullopt;
}

struct VisibleRoll {
    std::int32_t value{};
    std::size_t slot{};
};

std::optional<VisibleRoll> RollForRange(std::string_view line, const ModifierRange& range,

    const TooltipLocalization* localization) {
    const auto values = SignedIntegers(line);
    if (values.empty()) return std::nullopt;
    if (const auto slot = CompoundDamageSlot(line, range, localization)) {
        if (*slot >= values.size()) return std::nullopt;
        return VisibleRoll{values[*slot], *slot};
    }
    return VisibleRoll{values.front(), 0};
}

bool DescriptionFits(std::string_view line, const ModifierRange& range,
    const TooltipLocalization* localization) {
    const auto anchor = NormalizeAnchor(range.anchor);
    const auto normalizedLine = NormalizeWords(line);
    const auto cleanLine = StripColors(line);
    const auto firstVisible = cleanLine.find_first_not_of(" \t\r");
    const auto hasExplicitSign = firstVisible != std::string::npos
        && (cleanLine[firstVisible] == '+' || cleanLine[firstVisible] == '-');
    if (localization) {
        if (std::any_of(localization->metadataTemplates.begin(),
                localization->metadataTemplates.end(), [&](const auto& format) {
                    return MatchesLocalizedTemplate(line, format);
                })) return false;
        if (CompoundDamageSlot(line, range, localization)) return true;
        if (const auto found = localization->statTemplates.find(
                std::string(RangeStatKey(range)));
            found != localization->statTemplates.end()
            && std::any_of(found->second.begin(), found->second.end(),
                [&](const auto& format) {
                    return MatchesLocalizedTemplate(line, format,
                        !range.parameter.empty());
                })) return true;
    }
    if (anchor.empty()) return false;
    // Structural item lines are metadata, not rolled modifiers. Without this
    // gate, "Required Strength: 110" can match a variable Strength affix and
    // reject the item's complete candidate. Keep the patterns specific so
    // real modifiers such as Defense vs. Missile remain eligible.
    constexpr std::array metadataPrefixes{
        "required ", "one hand damage ", "two hand damage ", "throw damage ",
        "durability ", "chance to block ", "item level ", "affix level ",
        "base defense ", "max sockets ", "socketed ", "sell value ",
        "cost ", "quantity "};
    if ((normalizedLine == "defense" && !hasExplicitSign)
        || std::any_of(metadataPrefixes.begin(), metadataPrefixes.end(),
            [&](std::string_view prefix) {
                return normalizedLine.starts_with(prefix)
                    || (prefix.ends_with(' ')
                        && normalizedLine == prefix.substr(0, prefix.size() - 1));
            }))
        return false;
    if (CompoundDamageSlot(line, range, nullptr)) return true;
    auto position = std::size_t{};
    auto start = std::size_t{};
    while (start < anchor.size()) {
        const auto end = anchor.find(' ', start);
        const auto word = anchor.substr(start, end - start);
        position = normalizedLine.find(word, position);
        if (position == std::string::npos) return false;
        position += word.size();
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!range.parameter.empty()) {
        const auto parameter = NormalizeWords(range.parameter);
        const auto numericParameter = std::all_of(range.parameter.begin(), range.parameter.end(),
            [](unsigned char ch) { return std::isdigit(ch) != 0; });
        // Numeric skill/tab/monster ids are not rendered literally. They are
        // safe only when a native localized stat template matched above.
        if (numericParameter || (!parameter.empty()
            && normalizedLine.find(parameter) == std::string::npos)) return false;
    }
    return true;
}

bool RollFits(std::int32_t roll, const ModifierRange& range) {
    const auto magnitude = std::llabs(static_cast<long long>(roll));
    const auto low = std::min(
        std::llabs(static_cast<long long>(range.minimum)),
        std::llabs(static_cast<long long>(range.maximum)));

    const auto high = std::max(
        std::llabs(static_cast<long long>(range.minimum)),
        std::llabs(static_cast<long long>(range.maximum)));
    return magnitude >= low && magnitude <= high;
}

void AddRecord(const std::vector<std::vector<ModifierRange>>& records,
    std::size_t index, std::vector<ModifierRange>& output) {
    if (index < records.size()) {
        output.insert(output.end(), records[index].begin(), records[index].end());
    }
}

std::vector<ModifierRange> Combine(std::vector<ModifierRange> raw) {
    std::map<std::string, ModifierRange> combined;
    for (auto& range : raw) {
        auto [entry, inserted] = combined.try_emplace(range.key, range);
        if (!inserted) {
            entry->second.minimum += range.minimum;
            entry->second.maximum += range.maximum;
            entry->second.priority = std::max(entry->second.priority, range.priority);
        }
    }
    std::vector<ModifierRange> result;
    for (auto& [_, range] : combined) result.push_back(std::move(range));
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.priority > right.priority;
    });
    return result;
}

std::optional<RangeCatalog::PropertyInfo> DecodeProperty(
    const Row& row, std::size_t slot,
    const std::unordered_map<std::string, std::int32_t>& priorities) {
    const auto number = std::to_string(slot);
    const auto code = Lower(Get(row, "code"));
    const auto function = Number(row, "func" + number);
    auto stat = Lower(Get(row, "stat" + number));
    if (code.empty() || function == 0) return std::nullopt;
    if (function == 5) stat = "mindamage";
    else if (function == 6) stat = "maxdamage";
    else if (function == 7) stat = "enhanced_damage";
    else if (function != 1 && function != 2 && function != 3 && function != 4
        && function != 8 && function != 9 && function != 10 && function != 13
        && function != 15 && function != 16 && function != 21
        && function != 22 && function != 24) return std::nullopt;
    if (stat.empty()) return std::nullopt;
    const auto priority = priorities.contains(stat) ? priorities.at(stat) : 0;
    const auto parameterized = function == 9 || function == 10 || function == 21
        || function == 22 || function == 24;
    // Function 21 takes its parameter from properties.txt/val#. Functions 9,
    // 10, 22 and 24 take the parameter supplied by the owning table row; a
    // stray val# comment/helper value must not override that runtime input.
    auto fixedParameter = function == 21 ? Trim(Get(row, "val" + number)) : std::string{};
    return RangeCatalog::PropertyInfo{
        stat, slot == 1 ? Get(row, "*tooltip") : std::string{}, priority,
        function, parameterized, std::move(fixedParameter)};
}

ModifierRange MakeRange(const std::string& code, const RangeCatalog::PropertyInfo& property,
    std::string parameter, std::int32_t minimum, std::int32_t maximum) {
    // Functions 2/4 cannot be collapsed to the owner table's maximum here.
    // D2R's generated affix/runeword records demonstrably retain their
    // min/max roll (ac% is the canonical case: Saintly 66-80, Exile 220-260).
    // The function describes how the compiled property is applied, while the
    // owning TXT row still defines the possible roll envelope presented to
    // the player. Functions 15/16 are different: their two columns encode the
    // selected minimum or maximum component itself.
    if (property.function == 16) {
        minimum = maximum;
    } else if (property.function == 15) {
        maximum = minimum;
    }
    if (minimum > maximum) std::swap(minimum, maximum);
    auto key = property.key;
    if (property.parameterized) {
        if (!property.fixedParameter.empty()) parameter = property.fixedParameter;
        parameter = Trim(std::move(parameter));
        key += ":" + code + ":" + Lower(parameter);
    } else {

        parameter.clear();
    }
    return {std::move(key), property.anchor, minimum, maximum, property.priority,
        std::move(parameter), property.key};
}

void AppendPropertyRanges(
    const std::unordered_map<std::string,
        std::vector<RangeCatalog::PropertyInfo>>& properties,
    const std::string& code, std::string parameter, std::int32_t minimum,
    std::int32_t maximum, std::vector<ModifierRange>& result) {
    // Property functions 15/16 use the TXT minimum as the visible minimum
    // damage and the TXT maximum as the visible maximum damage. Model both
    // components separately so a collapsed "Adds X-Y Damage" tooltip can
    // still combine affixes, jewels, gems and runes without treating X and Y
    // as a single scalar roll.
    constexpr std::array compositeDamage{
        std::array<std::string_view, 3>{"dmg-fire", "fire-min", "fire-max"},
        std::array<std::string_view, 3>{"dmg-ltng", "ltng-min", "ltng-max"},
        std::array<std::string_view, 3>{"dmg-mag", "mag-min", "mag-max"},
        std::array<std::string_view, 3>{"dmg-cold", "cold-min", "cold-max"},
        std::array<std::string_view, 3>{"dmg-norm", "dmg-min", "dmg-max"}
    };
    if (const auto composite = std::find_if(compositeDamage.begin(), compositeDamage.end(),
            [&](const auto& entry) { return entry[0] == code; });
        composite != compositeDamage.end()) {
        const auto low = properties.find(std::string((*composite)[1]));
        const auto high = properties.find(std::string((*composite)[2]));
        if (low != properties.end() && !low->second.empty()
            && high != properties.end() && !high->second.empty()) {
            result.push_back(MakeRange(std::string((*composite)[1]), low->second.front(), {},
                minimum, minimum));
            result.push_back(MakeRange(std::string((*composite)[2]), high->second.front(), {},
                maximum, maximum));
        }
        return;
    }
    const auto property = properties.find(code);
    if (property == properties.end() || property->second.empty()) return;
    auto display = MakeRange(code, property->second.front(), parameter, minimum, maximum);
    const auto appendComposite = [&](std::initializer_list<std::string_view> components) {
        display.key = "display:" + code;
        display.statKey = "group:" + property->second.front().key;
        result.push_back(display);
        for (const auto component : components) {
            const auto found = properties.find(std::string(component));
            if (found == properties.end() || found->second.empty()) continue;
            result.push_back(MakeRange(std::string(component), found->second.front(),
                parameter, minimum, maximum));
        }
    };
    if (code == "res-all") {
        appendComposite({"res-fire", "res-ltng", "res-cold", "res-pois"});
    } else if (code == "all-stats") {
        appendComposite({"str", "dex", "vit", "enr"});
    } else {
        for (const auto& definition : property->second)
            result.push_back(MakeRange(code, definition, parameter, minimum, maximum));
    }
}

std::vector<ModifierRange> ReadMagicModifiers(const Row& row,
    const std::unordered_map<std::string,
        std::vector<RangeCatalog::PropertyInfo>>& properties) {
    std::vector<ModifierRange> result;
    for (std::size_t slot = 1; slot <= 3; ++slot) {
        const auto number = std::to_string(slot);
        const auto code = Lower(Get(row, "mod" + number + "code"));
        AppendPropertyRanges(properties, code, Get(row, "mod" + number + "param"),
            Number(row, "mod" + number + "min"),
            Number(row, "mod" + number + "max"), result);
    }
    return result;
}

std::vector<ModifierRange> ReadItemModifiers(const Row& row, std::size_t count,
    const std::unordered_map<std::string,
        std::vector<RangeCatalog::PropertyInfo>>& properties,
    std::string_view codePrefix = "prop", std::string_view minPrefix = "min",
    std::string_view maxPrefix = "max", std::string_view paramPrefix = "par") {

    std::vector<ModifierRange> result;
    for (std::size_t slot = 1; slot <= count; ++slot) {
        const auto number = std::to_string(slot);
        const auto code = Lower(Get(row, std::string(codePrefix) + number));
        AppendPropertyRanges(properties, code, Get(row, std::string(paramPrefix) + number),
            Number(row, std::string(minPrefix) + number),
            Number(row, std::string(maxPrefix) + number), result);
    }
    return result;
}

std::string StripRecipeQuotes(std::string value) {
    value = Trim(std::move(value));
    while (!value.empty() && value.front() == '"') value.erase(value.begin());
    while (!value.empty() && value.back() == '"') value.pop_back();
    return Lower(Trim(std::move(value)));
}

std::vector<std::string> RecipeTokens(std::string value) {
    std::vector<std::string> result;
    std::size_t start{};
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        auto token = StripRecipeQuotes(value.substr(start, comma - start));
        if (!token.empty()) result.push_back(std::move(token));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

std::string RecipeInputToken(std::string value) {
    auto tokens = RecipeTokens(std::move(value));
    return tokens.empty() ? std::string{} : std::move(tokens.front());
}

std::optional<std::uint32_t> RecipeQuality(std::string_view token) {
    if (token == "low") return 1;
    if (token == "nor") return 2;
    if (token == "hiq") return 3;
    if (token == "mag") return 4;
    if (token == "set") return 5;
    if (token == "rar") return 6;
    if (token == "uni") return 7;
    if (token == "crf") return 8;
    return std::nullopt;
}

bool SameCandidate(const std::vector<ModifierRange>& left,
    const std::vector<ModifierRange>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].key != right[index].key
            || left[index].minimum != right[index].minimum
            || left[index].maximum != right[index].maximum
            || left[index].parameter != right[index].parameter) return false;
    }
    return true;
}

enum class SocketCategory { Unknown, Weapon, Armor, Shield };

SocketCategory FindSocketCategory(
    const std::unordered_map<std::string, std::vector<std::string>>& itemTypes,
    std::string_view itemCode) {
    const auto types = itemTypes.find(Lower(std::string(itemCode)));
    if (types == itemTypes.end()) return SocketCategory::Unknown;
    if (std::find(types->second.begin(), types->second.end(), "shld") != types->second.end())
        return SocketCategory::Shield;
    if (std::find(types->second.begin(), types->second.end(), "weap") != types->second.end())
        return SocketCategory::Weapon;
    if (std::find(types->second.begin(), types->second.end(), "armo") != types->second.end())
        return SocketCategory::Armor;
    return SocketCategory::Unknown;
}

} // namespace

bool RangeCatalog::Load(const std::filesystem::path& excelDirectory, std::string& error) {
    return Load([&](std::string_view tableName, std::string& text, std::string& loadError) {

        const auto path = excelDirectory / tableName;
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            loadError = "Cannot open " + path.string();
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }, error);
}

bool RangeCatalog::LoadLayeredTable(
    const std::vector<std::filesystem::path>& excelDirectories,
    const TableTextProvider& fallback,
    std::string_view tableName,
    std::string& text,
    std::string& error,
    std::size_t& physicalLoads,
    std::size_t& fallbackLoads
) {
    for (const auto& directory : excelDirectories) {
        const auto path = directory / tableName;
        std::error_code inspectionError;
        const auto exists = std::filesystem::exists(path, inspectionError);
        if (inspectionError) {
            error = "Cannot inspect " + path.string() + ": " + inspectionError.message();
            return false;
        }
        if (!exists) continue;
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot open " + path.string();
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        ++physicalLoads;
        return true;
    }

    for (const auto& directory : excelDirectories) {
        auto binary = directory / tableName;
        binary.replace_extension(".bin");
        std::error_code inspectionError;
        const auto exists = std::filesystem::exists(binary, inspectionError);
        if (inspectionError) {
            error = "Cannot inspect " + binary.string() + ": " + inspectionError.message();
            return false;
        }
        if (!exists) continue;
        error = "Cannot safely model modified binary table without matching TXT: "
            + binary.string();
        return false;
    }

    if (!fallback) {
        error = "No TXT, BIN, or fallback is available for table: " + std::string(tableName);
        return false;
    }
    if (!fallback(tableName, text, error)) return false;
    ++fallbackLoads;
    return true;
}

bool RangeCatalog::Load(const TableTextProvider& provider, std::string& error) {
    const auto read = [&](std::string_view tableName, std::vector<Row>& rows) {
        std::string text;
        if (!provider(tableName, text, error)) return false;
        return ParseTsv(tableName, text, rows, error);
    };

    properties_.clear(); unsupportedPropertyFunctions_.clear();
    statStringKeys_.clear(); suffixes_.clear(); prefixes_.clear(); automagic_.clear();
    superiors_.clear();
    uniques_.clear(); sets_.clear(); armor_.clear(); itemTypes_.clear(); crafts_.clear();
    uniqueTokens_.clear(); setTokens_.clear();
    runes_.clear(); runewords_.clear(); runewordKeys_.clear();

    std::vector<Row> stats, properties;
    if (!read("itemstatcost.txt", stats)
        || !read("properties.txt", properties)) return false;

    std::unordered_map<std::string, std::int32_t> priorities;
    for (const auto& row : stats) {
        const auto stat = Lower(Get(row, "stat"));
        if (stat.empty()) continue;
        priorities[stat] = Number(row, "descpriority");
        auto& stringKeys = statStringKeys_[stat];
        for (const auto key : {"descstrpos", "descstrneg", "descstr2"}) {
            const auto value = Trim(Get(row, key));
            if (!value.empty()
                && std::find(stringKeys.begin(), stringKeys.end(), value) == stringKeys.end())
                stringKeys.push_back(value);
        }
        // Group descriptions (notably All Resistances and All Attributes)
        // are rendered separately from each component stat. Keeping a dgrp
        // format on fireresist/strength makes one grouped line match every
        // component and forces range consensus to fail closed.
        auto& groupStringKeys = statStringKeys_["group:" + stat];
        for (const auto key : {"dgrpstrpos", "dgrpstrneg", "dgrpstr2"}) {
            const auto value = Trim(Get(row, key));
            if (!value.empty()
                && std::find(groupStringKeys.begin(), groupStringKeys.end(), value)
                    == groupStringKeys.end())
                groupStringKeys.push_back(value);
        }
    }
    if (const auto damage = statStringKeys_.find("damagepercent");
        damage != statStringKeys_.end()) {
        statStringKeys_["enhanced_damage"] = damage->second;
    }
    for (const auto& row : properties) {
        const auto code = Lower(Get(row, "code"));
        if (code.empty()) continue;
        auto& definitions = properties_[code];
        for (std::size_t slot = 1; slot <= 7; ++slot) {
            const auto function = Number(row, "func" + std::to_string(slot));
            if (const auto decoded = DecodeProperty(row, slot, priorities)) {
                definitions.push_back(*decoded);
            } else if (function != 0) {
                ++unsupportedPropertyFunctions_[function];
            }
        }
        if (definitions.empty()) properties_.erase(code);
    }

    auto loadMagic = [&](std::string_view file, std::vector<std::vector<ModifierRange>>& target) {
        std::vector<Row> rows;
        if (!read(file, rows)) return false;
        target.reserve(rows.size() + 1);
        target.emplace_back(); // Affix id 0 means no affix.
        for (const auto& row : rows) {
            // The TXT compiler treats the named Expansion row as a section
            // delimiter, not as a compiled affix record. Keeping it in the
            // vector shifts every later runtime id by one.
            if (Lower(Get(row, "name")) == "expansion") continue;
            target.push_back(ReadMagicModifiers(row, properties_));
        }
        return true;
    };
    if (!loadMagic("magicsuffix.txt", suffixes_)
        || !loadMagic("magicprefix.txt", prefixes_)
        || !loadMagic("automagic.txt", automagic_)) return false;

    // Superior quality stores the selected qualityitems.txt row in fileIndex.
    // Unlike the three affix tables, qualityitems has no reserved zero id and
    // its records are compiled in physical row order.
    std::vector<Row> superiorRows;
    if (!read("qualityitems.txt", superiorRows)) return false;
    for (const auto& row : superiorRows)
        superiors_.push_back(ReadMagicModifiers(row, properties_));

    auto loadItems = [&](std::string_view file, std::size_t count,
        std::vector<std::vector<ModifierRange>>& target,
        std::vector<std::string>& tokens) {
        std::vector<Row> rows;
        if (!read(file, rows)) return false;
        target.clear();
        tokens.clear();
        for (const auto& row : rows) {
            // The TXT compiler consumes the named Expansion delimiter but
            // compiles every other physical row, including blank section

            // records. Runtime fileIndex therefore follows this derived order;
            // *ID is only a modder comment and may be absent, stale or reused.
            if (Lower(Trim(Get(row, "index"))) == "expansion") continue;
            target.push_back(ReadItemModifiers(row, count, properties_));
            tokens.push_back(Lower(Trim(Get(row, "index"))));
        }
        return true;
    };
    if (!loadItems("uniqueitems.txt", 12, uniques_, uniqueTokens_)) return false;

    std::vector<Row> setRows;
    if (!read("setitems.txt", setRows)) return false;
    sets_.clear();
    setTokens_.clear();
    for (const auto& row : setRows) {
        if (Lower(Trim(Get(row, "index"))) == "expansion") continue;
        SetRecord record;
        record.intrinsic = ReadItemModifiers(row, 9, properties_);
        record.addFunction = Number(row, "add func");
        record.conditionalGroups.resize(5);
        for (std::size_t group = 1; group <= 5; ++group) {
            auto& modifiers = record.conditionalGroups[group - 1];
            for (const auto side : {'a', 'b'}) {
                const auto suffix = std::to_string(group) + side;
                AppendPropertyRanges(properties_,
                    Lower(Get(row, "aprop" + suffix)), Get(row, "apar" + suffix),
                    Number(row, "amin" + suffix), Number(row, "amax" + suffix),
                    modifiers);
            }
        }
        sets_.push_back(std::move(record));
        setTokens_.push_back(Lower(Trim(Get(row, "index"))));
    }

    std::vector<Row> armor;
    if (!read("armor.txt", armor)) return false;
    for (const auto& row : armor) {
        const auto code = Lower(Get(row, "code"));
        if (!code.empty()) armor_[code] = {Number(row, "minac"), Number(row, "maxac")};
    }

    std::unordered_map<std::string, std::vector<std::string>> typeParents;
    std::vector<Row> typeRows;
    if (!read("itemtypes.txt", typeRows)) return false;
    for (const auto& row : typeRows) {
        const auto code = Lower(Get(row, "code"));
        if (code.empty()) continue;
        for (const auto key : {"equiv1", "equiv2"}) {
            const auto parent = Lower(Get(row, key));
            if (!parent.empty()) typeParents[code].push_back(parent);
        }
    }
    auto addItemTable = [&](std::string_view file) {
        std::vector<Row> rows;
        if (!read(file, rows)) return false;
        for (const auto& row : rows) {
            const auto code = Lower(Get(row, "code"));
            const auto type = Lower(Get(row, "type"));
            if (code.empty() || type.empty()) continue;
            std::unordered_set<std::string> seen;
            std::vector<std::string> pending{type};
            while (!pending.empty()) {
                auto current = pending.back(); pending.pop_back();
                if (!seen.insert(current).second) continue;
                itemTypes_[code].push_back(current);
                if (const auto found = typeParents.find(current); found != typeParents.end())
                    pending.insert(pending.end(), found->second.begin(), found->second.end());
            }
        }
        return true;
    };
    if (!addItemTable("weapons.txt") || !addItemTable("armor.txt") || !addItemTable("misc.txt")) return false;

    std::vector<Row> gems;
    if (!read("gems.txt", gems)) return false;
    const auto readGemModifiers = [&](const Row& row, std::string_view group) {
        std::vector<ModifierRange> result;
        for (std::size_t slot = 1; slot <= 3; ++slot) {
            const auto stem = std::string(group) + "mod" + std::to_string(slot);
            const auto code = Lower(Get(row, stem + "code"));

            AppendPropertyRanges(properties_, code, Get(row, stem + "param"),
                Number(row, stem + "min"), Number(row, stem + "max"), result);
        }
        return result;
    };
    for (const auto& row : gems) {
        const auto code = Lower(Get(row, "code"));
        if (code.empty()) continue;
        runes_[code] = {
            readGemModifiers(row, "weapon"),
            readGemModifiers(row, "helm"),
            readGemModifiers(row, "shield")
        };
    }

    std::vector<Row> runewords;
    if (!read("runes.txt", runewords)) return false;
    for (const auto& row : runewords) {
        if (Number(row, "complete") == 0) continue;
        const auto key = Trim(Get(row, "name"));
        if (key.empty()) continue;
        RunewordRecord record;
        record.modifiers = ReadItemModifiers(
            row, 7, properties_, "t1code", "t1min", "t1max", "t1param");
        for (std::size_t slot = 1; slot <= 6; ++slot) {
            const auto rune = Lower(Trim(Get(row, "rune" + std::to_string(slot))));
            if (!rune.empty()) record.runes.push_back(rune);
        }
        auto [entry, inserted] = runewords_.try_emplace(key);
        entry->second.push_back(std::move(record));
        if (inserted) runewordKeys_.push_back(key);
    }

    std::vector<Row> cube;
    if (!read("cubemain.txt", cube)) return false;
    for (const auto& row : cube) {
        if (Number(row, "enabled") == 0) continue;
        const auto outputTokens = RecipeTokens(Get(row, "output"));
        if (outputTokens.empty() || outputTokens.front() != "usetype"
            || std::find(outputTokens.begin() + 1, outputTokens.end(), "crf")
                == outputTokens.end()) continue;
        const auto input = RecipeInputToken(Get(row, "input 1"));
        auto fixed = ReadItemModifiers(row, 5, properties_, "mod ", "mod ", "mod ");
        // Cube columns are "mod N", "mod N min", and "mod N max".
        fixed.clear();
        for (std::size_t slot = 1; slot <= 5; ++slot) {
            const auto number = std::to_string(slot);
            const auto code = Lower(Get(row, "mod " + number));
            auto minimum = Number(row, "mod " + number + " min");
            auto maximum = Number(row, "mod " + number + " max");
            AppendPropertyRanges(properties_, code, Get(row, "mod " + number + " param"),
                minimum, maximum, fixed);
        }
        if (!input.empty() && !fixed.empty()) crafts_[input].push_back(std::move(fixed));
    }
    return true;
}

TooltipLocalization RangeCatalog::BuildLocalization(
    const LocalizedStringResolver& resolver) const {
    return BuildTooltipLocalization(statStringKeys_, resolver);
}

CandidateResolution RangeCatalog::ResolveCandidateSet(
    const ItemAffixIds& ids, std::string_view itemCode, std::string_view runewordKey,
    bool includeSocketedContributions, const StatReader&,
    std::string_view, const TooltipLocalization*) const {
    std::vector<ModifierRange> affixes;
    const auto suffixCount = suffixes_.empty() ? 0U : suffixes_.size() - 1U;
    const auto prefixCount = prefixes_.empty() ? 0U : prefixes_.size() - 1U;
    auto suffix = [&](std::uint16_t id) { AddRecord(suffixes_, id, affixes); };
    auto prefix = [&](std::uint16_t id) {
        AddRecord(prefixes_, id > suffixCount ? id - suffixCount : id, affixes);
    };
    // rarePrefix/rareSuffix choose the generated rare/crafted display name
    // (for example, "Stone Razor"). They are 8-bit RarePrefix/RareSuffix
    // table ids, not MagicPrefix/MagicSuffix stat affixes. Only the three
    // magic prefix/suffix slots below contribute item properties.
    // A runeword is created from a non-magic base. Its six magic affix slots
    // are not authoritative after IFLAG_RUNEWORD is set (the first one is

    // explicitly repurposed as the runeword string id, and the remaining
    // bytes can retain runtime payload). Never decode any of them as affixes.
    if (!ids.runeword) {
        for (const auto id : ids.magicPrefix) prefix(id);
        for (const auto id : ids.magicSuffix) suffix(id);
    }
    if (ids.autoPrefix) {
        const auto offset = suffixCount + prefixCount;
        AddRecord(automagic_, ids.autoPrefix > offset ? ids.autoPrefix - offset : ids.autoPrefix, affixes);
    }
    if (ids.quality == 7) AddRecord(uniques_, ids.fileIndex, affixes);
    std::vector<std::vector<ModifierRange>> itemCandidates;
    if (ids.quality == 5 && ids.fileIndex < sets_.size()) {
        const auto& set = sets_[ids.fileIndex];
        affixes.insert(affixes.end(), set.intrinsic.begin(), set.intrinsic.end());
        const auto appendGroup = [&](std::vector<ModifierRange>& candidate,
            std::size_t group) {
            if (group >= set.conditionalGroups.size()) return;
            candidate.insert(candidate.end(), set.conditionalGroups[group].begin(),
                set.conditionalGroups[group].end());
        };
        if (set.addFunction == 0) {
            for (std::size_t group = 0; group < set.conditionalGroups.size(); ++group)
                appendGroup(affixes, group);
            itemCandidates.push_back(std::move(affixes));
        } else if (set.addFunction == 1) {
            // Each group belongs to a particular companion piece. The item
            // does not persist the equipped-set mask, so retain every subset
            // and let whole-tooltip consensus select only a proven state.
            const auto combinations = std::size_t{1} << set.conditionalGroups.size();
            itemCandidates.reserve(combinations);
            for (std::size_t mask = 0; mask < combinations; ++mask) {
                auto candidate = affixes;
                for (std::size_t group = 0; group < set.conditionalGroups.size(); ++group)
                    if ((mask & (std::size_t{1} << group)) != 0)
                        appendGroup(candidate, group);
                itemCandidates.push_back(std::move(candidate));
            }
        } else if (set.addFunction == 2) {
            // Count-based bonuses are cumulative prefixes: zero through five
            // active groups cover every possible number of equipped pieces.
            itemCandidates.reserve(set.conditionalGroups.size() + 1);
            auto candidate = affixes;
            itemCandidates.push_back(candidate);
            for (std::size_t group = 0; group < set.conditionalGroups.size(); ++group) {
                appendGroup(candidate, group);
                itemCandidates.push_back(candidate);
            }
        } else {
            // Unknown add-function semantics are fail-closed: preserve only
            // intrinsic properties instead of fabricating conditional rolls.
            itemCandidates.push_back(std::move(affixes));
        }
    } else {
        itemCandidates.push_back(std::move(affixes));
    }

    std::vector<std::vector<ModifierRange>> baseCandidates;
    if (!runewordKey.empty()) {
        const auto runeword = runewords_.find(std::string(runewordKey));
        if (runeword != runewords_.end()) {
            const auto category = FindSocketCategory(itemTypes_, itemCode);
            for (const auto& itemCandidate : itemCandidates) {
                for (const auto& record : runeword->second) {
                    auto candidate = itemCandidate;
                    candidate.insert(candidate.end(), record.modifiers.begin(),
                        record.modifiers.end());
                    if (includeSocketedContributions) {
                        for (const auto& runeCode : record.runes) {
                            const auto rune = runes_.find(runeCode);
                            if (rune == runes_.end() || category == SocketCategory::Unknown) continue;
                            const auto* modifiers = &rune->second.armor;
                            if (category == SocketCategory::Weapon) modifiers = &rune->second.weapon;
                            else if (category == SocketCategory::Shield) modifiers = &rune->second.shield;
                            candidate.insert(candidate.end(), modifiers->begin(), modifiers->end());
                        }
                    }
                    baseCandidates.push_back(std::move(candidate));
                }
            }

        }
    }
    if (baseCandidates.empty()) baseCandidates = std::move(itemCandidates);

    std::vector<std::vector<ModifierRange>> result;
    if (ids.quality == 3) {
        // Runtime captures prove that D2R's superior fileIndex is not mapped
        // consistently to the physical qualityitems.txt row across every
        // generated record. Preserve both observed interpretations as
        // alternatives. Whole-tooltip validation then selects the candidate
        // that can reproduce all visible superior properties instead of
        // applying an unsafe global offset or item-specific exception.
        for (const auto& base : baseCandidates) {
            for (const auto record : {ids.fileIndex, ids.fileIndex + 1U}) {
                auto candidate = base;
                AddRecord(superiors_, record, candidate);
                const auto combined = Combine(std::move(candidate));
                if (std::find_if(result.begin(), result.end(), [&](const auto& existing) {
                        if (existing.size() != combined.size()) return false;
                        for (std::size_t index = 0; index < existing.size(); ++index) {
                            const auto& left = existing[index];
                            const auto& right = combined[index];
                            if (left.key != right.key || left.minimum != right.minimum
                                || left.maximum != right.maximum
                                || left.parameter != right.parameter) return false;
                        }
                        return true;
                    }) == result.end()) result.push_back(combined);
            }
        }
    }
    if (ids.quality == 8) {
        std::set<const std::vector<ModifierRange>*> recipes;
        const auto collect = [&](std::string_view token) {
            if (const auto found = crafts_.find(std::string(token)); found != crafts_.end())
                for (const auto& recipe : found->second) recipes.insert(&recipe);
        };
        collect(Lower(std::string(itemCode)));
        if (const auto types = itemTypes_.find(Lower(std::string(itemCode))); types != itemTypes_.end())
            for (const auto& type : types->second) collect(type);
        for (const auto& base : baseCandidates)
            for (const auto* recipe : recipes) {
                auto candidate = base;
                candidate.insert(candidate.end(), recipe->begin(), recipe->end());
                result.push_back(Combine(std::move(candidate)));
            }
    }
    if (result.empty())
        for (auto& candidate : baseCandidates)
            result.push_back(Combine(std::move(candidate)));

    // Only properties that define the generated item are authoritative here.
    // In particular, usetype,crf creation rows above are intrinsic to crafted
    // items. Later useitem/usetype mutations do not leave a portable, complete
    // history on the finished item, so an inter-mod tooltip must not infer or
    // aggregate them on the mouse-hover path.
    auto intrinsicCandidates = result;
    return {std::move(result), std::move(intrinsicCandidates)};
}

std::vector<std::vector<ModifierRange>> RangeCatalog::ResolveCandidates(
    const ItemAffixIds& ids, std::string_view itemCode, std::string_view runewordKey,
    bool includeSocketedContributions, const StatReader& readStat) const {
    // This compatibility API returns only item-owned ranges, including the
    // fixed properties of the usetype,crf recipe that created a crafted item.
    return ResolveCandidateSet(ids, itemCode, runewordKey,
        includeSocketedContributions, readStat).candidates;
}

std::vector<std::vector<ModifierRange>> RangeCatalog::ResolveSocketFillerCandidates(
    const ItemAffixIds& ids, std::string_view fillerCode, std::string_view parentCode) const {
    auto result = ResolveCandidates(ids, fillerCode);
    const auto gem = runes_.find(Lower(std::string(fillerCode)));
    if (gem == runes_.end()) return result;

    const auto category = FindSocketCategory(itemTypes_, parentCode);
    if (category == SocketCategory::Unknown) return result;
    const auto* modifiers = &gem->second.armor;
    if (category == SocketCategory::Weapon) modifiers = &gem->second.weapon;
    else if (category == SocketCategory::Shield) modifiers = &gem->second.shield;


    for (auto& candidate : result) {
        candidate.insert(candidate.end(), modifiers->begin(), modifiers->end());
        candidate = Combine(std::move(candidate));
    }
    return result;
}

std::optional<ArmorRange> RangeCatalog::FindArmor(std::string_view code) const {
    const auto found = armor_.find(Lower(std::string(code)));
    return found == armor_.end() ? std::nullopt : std::optional(found->second);
}

std::vector<std::vector<ModifierRange>> MergeCandidateSources(
    const std::vector<std::vector<ModifierRange>>& parent,
    const std::vector<std::vector<ModifierRange>>& child) {
    if (child.empty()) return parent;
    if (parent.empty()) return child;
    std::vector<std::vector<ModifierRange>> merged;
    merged.reserve(parent.size() * child.size());
    for (const auto& parentCandidate : parent) {
        for (const auto& childCandidate : child) {
            auto candidate = parentCandidate;
            candidate.insert(candidate.end(), childCandidate.begin(), childCandidate.end());
            merged.push_back(Combine(std::move(candidate)));
        }
    }
    return merged;
}

std::optional<std::int32_t> FirstSignedInteger(std::string_view text) {
    const auto values = SignedIntegers(text);
    return values.empty() ? std::nullopt : std::optional(values.front());
}

std::optional<std::int32_t> ExactFlatDefenseTotal(
    std::string_view tooltip,
    const TooltipLocalization* localization) {
    if (localization) {
        std::int64_t localizedTotal{};
        const auto templates = localization->statTemplates.find("armorclass");
        if (templates == localization->statTemplates.end()) return 0;
        std::size_t localizedStart{};
        while (localizedStart <= tooltip.size()) {
            const auto end = tooltip.find('\n', localizedStart);
            const auto line = tooltip.substr(localizedStart, end - localizedStart);
            const auto cleanLine = StripTrailingRangeAnnotation(line);
            if (std::any_of(templates->second.begin(), templates->second.end(),
                    [&](const auto& format) {
                        return MatchesLocalizedTemplate(cleanLine, format);
                    })) {
                const auto values = SignedIntegers(cleanLine);
                if (values.size() != 1) return std::nullopt;
                localizedTotal += values.front();
                if (localizedTotal < std::numeric_limits<std::int32_t>::min()
                    || localizedTotal > std::numeric_limits<std::int32_t>::max())
                    return std::nullopt;
            }
            if (end == std::string_view::npos) break;
            localizedStart = end + 1;
        }
        return static_cast<std::int32_t>(localizedTotal);
    }
    std::int64_t total{};
    std::size_t start{};
    while (start <= tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        const auto line = Trim(StripColors(tooltip.substr(start, end - start)));
        if (line.size() >= 2 && (line.front() == '+' || line.front() == '-')) {
            std::size_t index = 1;
            while (index < line.size()
                && std::isdigit(static_cast<unsigned char>(line[index]))) ++index;
            if (index > 1) {
                std::int32_t magnitude{};
                const auto parsed = std::from_chars(
                    line.data() + 1, line.data() + index, magnitude);
                while (index < line.size()
                    && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
                constexpr std::string_view label = "Defense";
                if (parsed.ec == std::errc{}

                    && line.substr(index).starts_with(label)) {
                    index += label.size();
                    while (index < line.size()
                        && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
                    // Only an exact "+N Defense" property contributes flat
                    // armor. Do not mistake "Defense vs. Missile" or a
                    // character-level defense line for flat defense. A range
                    // suffix previously appended by this plugin is allowed.
                    if (index == line.size() || line[index] == '[') {
                        const auto value = line.front() == '-' ? -magnitude : magnitude;
                        total += value;
                        if (total < std::numeric_limits<std::int32_t>::min()
                            || total > std::numeric_limits<std::int32_t>::max()) {
                            return std::nullopt;
                        }
                    }
                }
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return static_cast<std::int32_t>(total);
}

std::optional<std::int32_t> ExactEnhancedDefensePercent(
    std::string_view tooltip,
    const TooltipLocalization* localization) {
    if (localization) {
        std::optional<std::int32_t> localizedResult;
        const auto templates = localization->statTemplates.find("item_armor_percent");
        if (templates == localization->statTemplates.end()) return 0;
        std::size_t localizedStart{};
        while (localizedStart <= tooltip.size()) {
            const auto end = tooltip.find('\n', localizedStart);
            const auto line = tooltip.substr(localizedStart, end - localizedStart);
            const auto cleanLine = StripTrailingRangeAnnotation(line);
            if (std::any_of(templates->second.begin(), templates->second.end(),
                    [&](const auto& format) {
                        return MatchesLocalizedTemplate(cleanLine, format);
                    })) {
                const auto values = SignedIntegers(cleanLine);
                if (values.size() != 1 || localizedResult) return std::nullopt;
                localizedResult = values.front();
            }
            if (end == std::string_view::npos) break;
            localizedStart = end + 1;
        }
        return localizedResult.value_or(0);
    }
    std::optional<std::int32_t> result;
    std::size_t start{};
    while (start <= tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        const auto line = Trim(StripColors(tooltip.substr(start, end - start)));
        if (line.size() >= 3 && (line.front() == '+' || line.front() == '-')) {
            std::size_t index = 1;
            while (index < line.size()
                && std::isdigit(static_cast<unsigned char>(line[index]))) ++index;
            if (index > 1) {
                std::int32_t magnitude{};
                const auto parsed = std::from_chars(
                    line.data() + 1, line.data() + index, magnitude);
                if (index < line.size() && line[index] == '%') ++index;
                while (index < line.size()
                    && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
                constexpr std::string_view label = "Enhanced Defense";
                if (parsed.ec == std::errc{}
                    && line.substr(index).starts_with(label)) {
                    index += label.size();
                    while (index < line.size()
                        && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
                    if (index == line.size() || line[index] == '[') {
                        const auto value = line.front() == '-' ? -magnitude : magnitude;
                        // The renderer normally aggregates this stat into one
                        // line. More than one exact line is ambiguous and must
                        // fail closed rather than double-counting it.
                        if (result) return std::nullopt;
                        result = value;
                    }

                }
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result.value_or(0);
}

std::optional<std::int32_t> ReconstructBaseDefense(
    std::int32_t finalDefense,
    std::int32_t enhancedDefensePercent,
    std::int32_t flatDefense,
    std::int32_t minimum,
    std::int32_t maximum,
    bool ethereal) {
    if (minimum < 0 || maximum < minimum || enhancedDefensePercent <= -100)
        return std::nullopt;

    const auto scaleBase = [ethereal](std::int32_t base) -> std::int64_t {
        return ethereal ? static_cast<std::int64_t>(base) * 3 / 2 : base;
    };
    const auto calculate = [&](std::int32_t base) -> std::int64_t {
        const auto physicalBase = scaleBase(base);
        return physicalBase * (100LL + enhancedDefensePercent) / 100 + flatDefense;
    };

    std::set<std::int32_t> displayedRolls;
    for (auto base = minimum; base <= maximum; ++base) {
        if (calculate(base) == finalDefense)
            displayedRolls.insert(static_cast<std::int32_t>(scaleBase(base)));
        if (base == std::numeric_limits<std::int32_t>::max()) break;
    }

    // Items generated with an inherent Enhanced Defense property use the
    // table maximum plus one as their physical base. The player-facing base
    // roll still belongs to the table range, so map that sentinel back to the
    // displayed maximum. This also covers superior ethereal armor.
    if (maximum < std::numeric_limits<std::int32_t>::max()
        && calculate(maximum + 1) == finalDefense) {
        displayedRolls.insert(static_cast<std::int32_t>(scaleBase(maximum)));
    }

    return displayedRolls.size() == 1
        ? std::optional(*displayedRolls.begin())
        : std::nullopt;
}

std::string FormatPositiveRange(
    std::int32_t minimum,
    std::int32_t maximum,
    char restoreColor,
    char rangeColor) {
    auto first = std::llabs(static_cast<long long>(minimum));
    auto second = std::llabs(static_cast<long long>(maximum));
    if (first > second) std::swap(first, second);
    std::string result(ColorMarker, 3);
    // Keep D2R's private renderer marker so the selected palette entry is
    // consumed as color metadata instead of falling back to another font.
    result.push_back(rangeColor);
    result += "[" + std::to_string(first) + " - " + std::to_string(second) + "]";
    result.append(ColorMarker, 3);
    result.push_back(restoreColor);
    return result;
}

std::string AppendConsensusRanges(std::string_view tooltip,
    const std::vector<std::vector<ModifierRange>>& candidates,
    bool allowExcludedSocketContributions,
    const TooltipLocalization* localization,
    const std::vector<std::vector<ModifierRange>>* intrinsicFallback,
    char rangeColor) {
    if (candidates.empty() && (!intrinsicFallback || intrinsicFallback->empty()))
        return std::string(tooltip);
    std::vector<std::string> lines;
    std::size_t start{};
    while (start <= tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        lines.emplace_back(tooltip.substr(start, end - start));
        if (end == std::string_view::npos) break;

        start = end + 1;
    }

    // Crafted items do not retain the cubemain recipe that created them. Use
    // the complete rendered tooltip to eliminate recipe candidates that
    // cannot reproduce a displayed stat before resolving any individual
    // line. A Caster Amulet with 15 FCR, for example, rejects the non-Caster
    // candidates whose fixed suffix can only produce 10 FCR; its Mana line can
    // then safely include the recipe's additional 10-20 Mana.
    const auto compatibleFor = [&](const std::vector<std::vector<ModifierRange>>& source) {
        std::vector<bool> modeledLines;
        modeledLines.reserve(lines.size());
        for (const auto& line : lines) {
            // A rendered value can include a source that is not represented by
            // the parent item's candidate (most notably a socketed jewel).
            // Only use a line as evidence when this source can reproduce it.
            modeledLines.push_back(!SignedIntegers(line).empty()
                && std::any_of(source.begin(), source.end(), [&](const auto& candidate) {
                    return std::any_of(candidate.begin(), candidate.end(),
                        [&](const auto& range) {
                            const auto roll = RollForRange(line, range, localization);
                            return DescriptionFits(line, range, localization) && roll
                                && RollFits(roll->value, range);
                        });
                }));
        }

        std::vector<const std::vector<ModifierRange>*> compatible;
        for (const auto& candidate : source) {
            bool fits = true;
            for (std::size_t index = 0; index < lines.size(); ++index) {
                if (!modeledLines[index]) continue;
                const auto producesRoll = std::any_of(candidate.begin(), candidate.end(),
                    [&](const auto& range) {
                        const auto roll = RollForRange(lines[index], range, localization);
                        return DescriptionFits(lines[index], range, localization)
                            && roll && RollFits(roll->value, range);
                    });
                if (!producesRoll) {
                    fits = false;
                    break;
                }
            }
            if (fits) compatible.push_back(&candidate);
        }
        return compatible;
    };

    const auto compatibleCandidates = compatibleFor(candidates);
    const auto fallbackCandidates = intrinsicFallback
        ? compatibleFor(*intrinsicFallback)
        : std::vector<const std::vector<ModifierRange>*>{};

    // D2 stores and parses final-tooltip lines bottom-to-top. A native color
    // marker may appear only on the first line of a modifier block; the lines
    // above it inherit that state. Track the renderer state in buffer order so
    // every dark-green suffix restores the actual native color.
    using ConsensusRange = std::tuple<std::size_t, std::string,
        std::int32_t, std::int32_t>;
    const auto consensusFor = [&](std::string_view line,
        const std::vector<const std::vector<ModifierRange>*>& compatible)
        -> std::optional<std::vector<ConsensusRange>> {
        std::optional<std::vector<ConsensusRange>> consensus;
        for (const auto* candidate : compatible) {
            std::vector<ConsensusRange> matches;
            for (const auto& range : *candidate) {
                if (range.minimum == range.maximum
                    || !DescriptionFits(line, range, localization)) continue;
                const auto roll = RollForRange(line, range, localization);
                // In intrinsic-only mode the rendered value can legitimately
                // sit outside the parent range because D2R has already folded
                // socket fillers into that line. The candidate still proves
                // that the parent owns this property; candidate consensus
                // below prevents an uncertain craft/superior alternative from
                // receiving a fabricated annotation.
                if (roll && (RollFits(roll->value, range)
                    || allowExcludedSocketContributions))
                    matches.emplace_back(roll->slot, range.key, range.minimum, range.maximum);
            }
            // Recipe identity is not stored on the item. All candidates that

            // survived whole-tooltip validation must agree exactly.
            // Every compatible candidate already proved that it can produce
            // this modeled line. If one explanation produces only a fixed
            // value while another owns a variable range, there is no range
            // consensus: annotating the variable alternative would claim a
            // recipe history that the finished item does not prove.
            if (matches.empty()) {
                return std::nullopt;
            }
            std::sort(matches.begin(), matches.end());
            if (std::adjacent_find(matches.begin(), matches.end(), [](const auto& left,
                    const auto& right) { return std::get<0>(left) == std::get<0>(right); })
                != matches.end()) {
                return std::nullopt;
            }
            if (!consensus) consensus = matches;
            else if (*consensus != matches) return std::nullopt;
        }
        return consensus;
    };

    char activeColor = '0';
    for (auto& line : lines) {
        activeColor = LastColor(line, activeColor);
        if (SignedIntegers(line).empty()) continue;
        auto consensus = consensusFor(line, compatibleCandidates);
        // When externally merged candidate sources disagree, retain the range
        // owned by the item itself rather than suppressing a valid vanilla or
        // custom unique, set, affix, craft, automagic or runeword range.
        if (!consensus && intrinsicFallback)
            consensus = consensusFor(line, fallbackCandidates);
        if (consensus) {
            line.push_back(' ');
            for (std::size_t index = 0; index < consensus->size(); ++index) {
                if (index) line += " "
                    + (localization ? localization->rangeSeparator : std::string("to"))
                    + " ";
                line += FormatPositiveRange(std::get<2>((*consensus)[index]),
                    std::get<3>((*consensus)[index]), activeColor, rangeColor);
            }
        }
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index) result.push_back('\n');
        result += lines[index];
    }
    return result;
}

} // namespace tcp::tooltips
