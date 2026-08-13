#include <plugin-shared.h>

static constexpr uint64_t OFF_GetStat = 0x2f5020; // FUN_1402f5020 -- see plugin-shared.h comment above PSh_GetStat

using GetStat_t = int64_t(__fastcall*)(int64_t unit, int statId, uint32_t extra);

extern "C" int PSh_GetStat(uintptr_t exeBase, D2UnitStrc* unit, int statId) noexcept {
    auto GetStat = reinterpret_cast<GetStat_t>(exeBase + OFF_GetStat);
    return static_cast<int>(GetStat(reinterpret_cast<int64_t>(unit), statId, 0));
}

extern "C" uint64_t PSh_RollUnit(D2UnitStrc* unit) noexcept {
	uint64_t next = static_cast<uint64_t>(unit->seedLow) * 0x6AC690C5ULL + unit->seedHigh;
	unit->seedLow  = static_cast<uint32_t>(next);
	unit->seedHigh = static_cast<uint32_t>(next >> 32);
	return next;
}