#pragma once

namespace RuffnecKk::ExtendedItemStats {

// Extended Item Stats is a built-in plugin-items patch, not a player option.
inline constexpr bool ItemTransportEnabled = true;
inline constexpr bool ScrollBarEnabledByDefault = true;

inline constexpr bool ShouldSuppressSecondaryNativeTooltip(
	bool overflowActive,
	bool contentMatches,
	const void* ownerText,
	const void* candidateText) noexcept {
	return overflowActive
		&& contentMatches
		&& ownerText
		&& candidateText
		&& ownerText != candidateText;
}

} // namespace RuffnecKk::ExtendedItemStats
