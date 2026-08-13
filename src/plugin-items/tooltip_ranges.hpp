#pragma once

#include "socket_tooltip.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tcp::tooltips {

struct ModifierRange {
    std::string key;
    std::string anchor;
    std::int32_t minimum{};
    std::int32_t maximum{};
    std::int32_t priority{};
    std::string parameter;
    std::string statKey;
};

struct ArmorRange {
    std::int32_t minimum{};
    std::int32_t maximum{};
};

struct ItemAffixIds {
    std::uint32_t quality{};
    std::uint32_t fileIndex{};
    bool runeword{};
    bool ethereal{};
    std::uint32_t socketCount{};
    std::uint16_t autoPrefix{};
    std::uint16_t rarePrefix{};
    std::uint16_t rareSuffix{};
    std::uint16_t magicPrefix[3]{};
    std::uint16_t magicSuffix[3]{};
};

struct CandidateResolution {
    // Every table-backed history intrinsic to the generated item.
    std::vector<std::vector<ModifierRange>> candidates;
    // A stable item-owned copy retained for callers that merge separately
    // inspectable sources such as socket fillers.
    std::vector<std::vector<ModifierRange>> intrinsicCandidates;
};

class RangeCatalog {
public:
    using TableTextProvider = std::function<bool(
        std::string_view tableName,
        std::string& text,
        std::string& error)>;
    using StatReader = std::function<std::int32_t(
        std::int32_t statId,
        std::uint16_t layer)>;

    struct PropertyInfo {
        std::string key;
        std::string anchor;
        std::int32_t priority{};
        std::int32_t function{};
        bool parameterized{};
        std::string fixedParameter;
    };

    bool Load(const std::filesystem::path& excelDirectory, std::string& error);
    bool Load(const TableTextProvider& provider, std::string& error);
    static bool LoadLayeredTable(
        const std::vector<std::filesystem::path>& excelDirectories,
        const TableTextProvider& fallback,
        std::string_view tableName,
        std::string& text,
        std::string& error,
        std::size_t& physicalLoads,
        std::size_t& fallbackLoads
    );
    [[nodiscard]] std::vector<std::vector<ModifierRange>> ResolveCandidates(
        const ItemAffixIds& ids,
        std::string_view itemCode,
        std::string_view runewordKey = {},
        bool includeSocketedContributions = true,
        const StatReader& readStat = {}
    ) const;
    [[nodiscard]] CandidateResolution ResolveCandidateSet(
        const ItemAffixIds& ids,
        std::string_view itemCode,
        std::string_view runewordKey = {},
        bool includeSocketedContributions = true,
        const StatReader& readStat = {},
        std::string_view renderedTooltip = {},
        const TooltipLocalization* localization = nullptr
    ) const;
    [[nodiscard]] std::vector<std::vector<ModifierRange>> ResolveSocketFillerCandidates(
        const ItemAffixIds& ids,
        std::string_view fillerCode,
        std::string_view parentCode
    ) const;
    [[nodiscard]] std::optional<ArmorRange> FindArmor(std::string_view code) const;
    [[nodiscard]] TooltipLocalization BuildLocalization(
        const LocalizedStringResolver& resolver
    ) const;
    [[nodiscard]] std::size_t PropertyCount() const noexcept { return properties_.size(); }
    [[nodiscard]] const std::map<std::int32_t, std::size_t>&
    UnsupportedPropertyFunctions() const noexcept {
        return unsupportedPropertyFunctions_;
    }
    [[nodiscard]] const std::vector<std::string>& RunewordKeys() const noexcept {
        return runewordKeys_;
    }

private:
    using PropertyDefinitions = std::vector<PropertyInfo>;
    std::unordered_map<std::string, PropertyDefinitions> properties_;
    std::map<std::int32_t, std::size_t> unsupportedPropertyFunctions_;
    std::unordered_map<std::string, std::vector<std::string>> statStringKeys_;
    std::vector<std::vector<ModifierRange>> suffixes_;
    std::vector<std::vector<ModifierRange>> prefixes_;
    std::vector<std::vector<ModifierRange>> automagic_;
    std::vector<std::vector<ModifierRange>> superiors_;
    std::vector<std::vector<ModifierRange>> uniques_;
    struct SetRecord {
        std::vector<ModifierRange> intrinsic;
        std::int32_t addFunction{};
        std::vector<std::vector<ModifierRange>> conditionalGroups;
    };
    std::vector<SetRecord> sets_;
    std::unordered_map<std::string, ArmorRange> armor_;
    std::unordered_map<std::string, std::vector<std::string>> itemTypes_;
    std::unordered_map<std::string, std::vector<std::vector<ModifierRange>>> crafts_;
    std::vector<std::string> uniqueTokens_;
    std::vector<std::string> setTokens_;
    struct RuneModifiers {
        std::vector<ModifierRange> weapon;
        std::vector<ModifierRange> armor;
        std::vector<ModifierRange> shield;
    };
    struct RunewordRecord {
        std::vector<ModifierRange> modifiers;
        std::vector<std::string> runes;
    };
    std::unordered_map<std::string, RuneModifiers> runes_;
    std::unordered_map<std::string, std::vector<RunewordRecord>> runewords_;
    std::vector<std::string> runewordKeys_;
};

[[nodiscard]] std::string AppendConsensusRanges(
    std::string_view tooltip,
    const std::vector<std::vector<ModifierRange>>& candidates,
    bool allowExcludedSocketContributions = false,
    const TooltipLocalization* localization = nullptr,
    const std::vector<std::vector<ModifierRange>>* intrinsicFallback = nullptr,
    char rangeColor = ':'
);

[[nodiscard]] std::vector<std::vector<ModifierRange>> MergeCandidateSources(
    const std::vector<std::vector<ModifierRange>>& parent,
    const std::vector<std::vector<ModifierRange>>& child
);

[[nodiscard]] std::string FormatPositiveRange(
    std::int32_t minimum,
    std::int32_t maximum,
    char restoreColor = '0',
    char rangeColor = ':'
);

[[nodiscard]] std::optional<std::int32_t> FirstSignedInteger(std::string_view text);

[[nodiscard]] std::optional<std::int32_t> ExactFlatDefenseTotal(
    std::string_view tooltip,
    const TooltipLocalization* localization = nullptr
);

[[nodiscard]] std::optional<std::int32_t> ExactEnhancedDefensePercent(
    std::string_view tooltip,
    const TooltipLocalization* localization = nullptr
);

[[nodiscard]] std::optional<std::int32_t> ReconstructBaseDefense(
    std::int32_t finalDefense,
    std::int32_t enhancedDefensePercent,
    std::int32_t flatDefense,
    std::int32_t minimum,
    std::int32_t maximum,
    bool ethereal
);

} // namespace tcp::tooltips
