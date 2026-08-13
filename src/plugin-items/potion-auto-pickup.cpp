#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include "potion-auto-pickup.h"
#include "potion-auto-pickup-policy.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
using namespace RuffnecKk::PotionAutoPickUp;

constexpr std::uintptr_t GetGameRva=0x34B440, EnumerateRva=0x2EFDE0;
constexpr std::uintptr_t FirstUnitRva=0x2EFD90, NextUnitRva=0x34B4A0, UnitTypeRva=0x34B9D0;
constexpr std::uintptr_t UnitIdRva=0x34A330, UnitModeRva=0x34AB60, UnitDistanceRva=0x325140;
constexpr std::uintptr_t UnitCollisionRva=0x350550, PickupRva=0x471950;
constexpr std::uintptr_t GetItemCodeRva=0x36EF50;
constexpr std::uintptr_t GetInventoryRva=0x34A360, ResolveOccupancyGridRva=0x38B070;
constexpr std::uintptr_t GetBeltTypeRva=0x349720, GetFreeBeltSlotRva=0x3862D0;
constexpr std::uintptr_t BodyGridInfoRva=0x237B620, BeltGridInfoRva=0x237B638;
constexpr std::uintptr_t ServerPacketTableRva=0x1D2A790;
constexpr std::uint32_t ItemType=4, GroundMode=3, PickupCollisionMask=0x804;
constexpr std::uint32_t SupportedBuild=92777;
constexpr std::uint8_t FirstTriggerOpcode=0x01, LastTriggerOpcode=0x12;
constexpr std::uint8_t BeltBodySlot=8;

constexpr std::array<std::uintptr_t,LastTriggerOpcode+1> TriggerHandlerRvas{
    0,
    0x4AC050,0x4ACE20,0x4ACE40,0x4ACE60,0x4ACE80,0x4ACF80,
    0x4AD030,0x4AD0E0,0x4AD100,0x4AD120,0x4AD140,0x4AD230,
    0x4AD330,0x4AD3E0,0x4AD490,0x4AD4B0,0x4AD4D0,0x4AD4F0
};
constexpr std::array<const char*,LastTriggerOpcode+1> TriggerManifestIds{
    nullptr,
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action01"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action02"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action03"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action04"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action05"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action06"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action07"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action08"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action09"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0A"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0B"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0C"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0D"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0E"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action0F"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action10"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action11"),
    PSH_MANIFEST_SITE("items.potionAutoPickUp.action12"),
};
constexpr std::array<std::uint8_t,32> GetFreeBeltSlotExpected{
    0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,
    0x56,0x41,0x57,0x48,0x81,0xEC,0x70,0x01,
    0x00,0x00,0x48,0x8B,0x05,0xDF,0x4F,0x64,
    0x02,0x48,0x33,0xC4,0x48,0x89,0x84,0x24
};
constexpr std::array<std::uint8_t,32> ResolveOccupancyGridExpected{
    0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x57,
    0x48,0x83,0xEC,0x30,0x49,0x8B,0xF8,0x48,
    0x39,0x51,0x28,0x0F,0x86,0x01,0x01,0x00,
    0x00,0x49,0x89,0x73,0x18,0x48,0x8D,0x71
};
constexpr std::array<std::uint8_t,32> GetInventoryExpected{
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,
    0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9,
    0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,
    0x4C,0x24,0x30,0xE8,0x70,0xCC,0xFF,0xFF
};
constexpr std::array<std::uint8_t,32> GetBeltTypeExpected{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,
    0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,
    0x75,0x15,0x88,0x4C,0x24,0x30,0x48,0x8D,
    0x4C,0x24,0x30,0xE8,0xE0,0xC0,0xFF,0xFF
};
constexpr std::array<std::uint8_t,32> GetItemCodeExpected{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,
    0xEC,0x20,0x48,0x8B,0xF9,0x48,0x85,0xC9,
    0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,
    0x4C,0x24,0x30,0xE8,0x80,0x83,0xFF,0xFF
};

struct PotionClass { std::uint32_t packedCode; std::string_view code; Family family; std::uint8_t tier; };
constexpr std::array<PotionClass,12> PotionClasses{
    PotionClass{PackItemCode("hp1"),"hp1",Family::Healing,1}, PotionClass{PackItemCode("hp2"),"hp2",Family::Healing,2},
    PotionClass{PackItemCode("hp3"),"hp3",Family::Healing,3}, PotionClass{PackItemCode("hp4"),"hp4",Family::Healing,4}, PotionClass{PackItemCode("hp5"),"hp5",Family::Healing,5},
    PotionClass{PackItemCode("mp1"),"mp1",Family::Mana,1}, PotionClass{PackItemCode("mp2"),"mp2",Family::Mana,2}, PotionClass{PackItemCode("mp3"),"mp3",Family::Mana,3},
    PotionClass{PackItemCode("mp4"),"mp4",Family::Mana,4}, PotionClass{PackItemCode("mp5"),"mp5",Family::Mana,5},
    PotionClass{PackItemCode("rvs"),"rvs",Family::Rejuvenation,1}, PotionClass{PackItemCode("rvl"),"rvl",Family::Rejuvenation,2},
};

using TriggerFn=std::int64_t(__fastcall*)(void*,void*,void*,std::int32_t);
using GetGameFn=void*(__fastcall*)(void*);
using EnumerateFn=void(__fastcall*)(void*,void***,std::uint32_t*);
using UnitFn=void*(__fastcall*)(void*);
using UnitIntFn=std::uint32_t(__fastcall*)(void*);
using UnitPairFn=std::int32_t(__fastcall*)(void*,void*);
using CollisionFn=std::int32_t(__fastcall*)(void*,void*,std::uint32_t);
using PickupFn=bool(__fastcall*)(void*,std::uint32_t,bool,std::uint32_t,bool,bool);
using GetItemCodeFn=std::uint32_t(__fastcall*)(void*);
using GetInventoryFn=void*(__fastcall*)(void*);
using GetBeltTypeFn=std::int32_t(__fastcall*)(void*);
using ResolveOccupancyGridFn=void*(__fastcall*)(void*,std::uint64_t,const void*);
using GetFreeBeltSlotFn=std::int32_t(__fastcall*)(void*,void*,std::int32_t*,bool);

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
std::array<TriggerFn,LastTriggerOpcode+1> OriginalTriggers{};
GetFreeBeltSlotFn OriginalGetFreeBeltSlot{};
GetGameFn GetGame{}; EnumerateFn Enumerate{}; UnitFn FirstUnit{},NextUnit{};
UnitIntFn UnitType{},UnitId{},UnitMode{}; UnitPairFn UnitDistance{}; CollisionFn UnitCollision{}; PickupFn Pickup{};
GetItemCodeFn GetItemCode{};
GetInventoryFn GetInventory{}; GetBeltTypeFn GetBeltType{}; ResolveOccupancyGridFn ResolveOccupancyGrid{};
Config Settings{};
struct RuntimeMetrics {
    std::atomic<std::uint64_t> actions{}, scans{}, beltStateFailures{}, enumerationFailures{};
    std::atomic<std::uint64_t> tierRejects{}, collisionRejects{}, distanceRejects{}, destinationRejects{};
    std::atomic<std::uint64_t> selections{}, beltRoutes{}, overflowRoutes{};
    std::atomic<std::uint64_t> routeMatches{}, routeMismatches{}, inventoryAliases{};
    std::atomic<std::uint64_t> pickupSuccesses{}, pickupFailures{};
    std::array<std::atomic<std::uint64_t>,PotionClasses.size()> seenByCode{};
    std::array<std::atomic<std::uint64_t>,PotionClasses.size()> selectedByCode{};
    std::array<std::atomic<std::uint64_t>,PotionClasses.size()> pickedByCode{};
};
RuntimeMetrics Metrics{};
thread_local bool Inside{};
thread_local std::uint32_t TriggerCounter{};
thread_local void* ForcedInventory{};
thread_local RoutingToken ForcedRoute{};
thread_local std::int32_t ForcedBeltSlot{-1};
thread_local bool ForceInventoryOverflow{};
thread_local bool LoggedScanException{};

const PotionClass* ClassifyPackedCode(std::uint32_t packedCode) {
    for(const auto& potion:PotionClasses) if(potion.packedCode==packedCode) return &potion;
    return nullptr;
}
const FamilyConfig& FamilySettings(Family family) {
    if(family==Family::Healing) return Settings.healing; if(family==Family::Mana) return Settings.mana; return Settings.rejuvenation;
}
bool Accepted(const PotionClass& potion) {
    return FamilySettings(potion.family).policy.Accepts({potion.code,potion.family,potion.tier});
}
std::uint8_t FamilyRank(Family family) {
    for(std::uint8_t index=0;index<Settings.familyPriorityCount;++index) {
        if(Settings.familyPriority[index]==family) return index;
    }
    return static_cast<std::uint8_t>(Settings.familyPriorityCount+static_cast<std::uint8_t>(family));
}
std::uint8_t TierRank(const FamilyConfig& config,std::uint8_t tier) {
    for(std::uint8_t index=0;index<config.tierPriorityCount;++index) {
        if(config.tierPriority[index]==tier) return index;
    }
    return static_cast<std::uint8_t>(config.tierPriorityCount+5-tier);
}
bool ReadBeltState(
    void* inventory,
    std::array<BeltSlot,16>& slots,
    std::uint8_t& capacity) noexcept {
    __try {
        if(!inventory || !GetBeltType || !ResolveOccupancyGrid) return false;
        auto* bodyGrid=static_cast<std::uint8_t*>(ResolveOccupancyGrid(
            inventory,0,Base+BodyGridInfoRva));
        if(!bodyGrid) return false;
        auto** bodyItems=*reinterpret_cast<void***>(bodyGrid+0x18);
        if(!bodyItems) return false;
        void* belt=bodyItems[BeltBodySlot];
        const auto beltType=belt?GetBeltType(belt):2;
        switch(beltType) {
        case 0: case 5: capacity=12; break;
        case 1: case 4: capacity=8; break;
        case 2: capacity=4; break;
        case 3: case 6: capacity=16; break;
        default: return false;
        }
        auto* beltGrid=static_cast<std::uint8_t*>(ResolveOccupancyGrid(
            inventory,1,Base+BeltGridInfoRva));
        if(!beltGrid) return false;
        const auto cells=static_cast<std::uint32_t>(beltGrid[0x10])*beltGrid[0x11];
        if(cells<capacity || cells>slots.size()) return false;
        auto** items=*reinterpret_cast<void***>(beltGrid+0x18);
        if(!items) return false;
        for(std::uint8_t index=0;index<capacity;++index) {
            if(!items[index]) continue;
            slots[index].occupied=true;
            if(const auto* potion=ClassifyPackedCode(GetItemCode(items[index]))) {
                slots[index].family=potion->family;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
std::string JoinCodes(Family family,const std::array<bool,6>& enabled) {
    std::string output;
    for(const auto& potion:PotionClasses) {
        if(potion.family!=family || !enabled[potion.tier]) continue;
        if(!output.empty()) output.push_back(',');
        output.append(potion.code);
    }
    return output.empty()?"none":output;
}
std::string JoinColumns(const Policy& policy) {
    std::string output;
    for(std::uint8_t index=0;index<policy.columnCount;++index) {
        if(!output.empty()) output.push_back(',');
        output.append(std::to_string(policy.columns[index]));
    }
    return output.empty()?"none":output;
}
std::string FamilySummary(std::string_view name,Family family,const FamilyConfig& config) {
    std::string output(name);
    output.append(" enabled=").append(config.policy.enabled?"true":"false");
    output.append(" tiers=").append(JoinCodes(family,config.policy.tiers));
    output.append(" columns=").append(JoinColumns(config.policy));
    output.append(" overflow=").append(JoinCodes(family,config.policy.overflowTiers));
    return output;
}
std::string Summary() {
    std::string output="PotionAutoPickUp 1.1.3 (plugin-items): enabled=";
    output.append(Settings.enabled?"true":"false")
        .append("; config=items.potionAutoPickUp; triggers=0x01-0x12; ");
    output.append(FamilySummary("healing",Family::Healing,Settings.healing)).append("; ");
    output.append(FamilySummary("mana",Family::Mana,Settings.mana)).append("; ");
    output.append(FamilySummary("rejuvenation",Family::Rejuvenation,Settings.rejuvenation)).append(".");
    return output;
}
std::size_t PotionIndex(const PotionClass& potion) noexcept {
    return static_cast<std::size_t>(&potion-PotionClasses.data());
}
std::uint64_t Value(const std::atomic<std::uint64_t>& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}
std::string MetricsSummary() {
    std::string output="PotionAutoPickup metrics actions="+std::to_string(Value(Metrics.actions));
    output.append(" scans=").append(std::to_string(Value(Metrics.scans)));
    output.append(" belt-state-failures=").append(std::to_string(Value(Metrics.beltStateFailures)));
    output.append(" enumeration-failures=").append(std::to_string(Value(Metrics.enumerationFailures)));
    output.append(" tier-rejects=").append(std::to_string(Value(Metrics.tierRejects)));
    output.append(" collision-rejects=").append(std::to_string(Value(Metrics.collisionRejects)));
    output.append(" distance-rejects=").append(std::to_string(Value(Metrics.distanceRejects)));
    output.append(" destination-rejects=").append(std::to_string(Value(Metrics.destinationRejects)));
    output.append(" selections=").append(std::to_string(Value(Metrics.selections)));
    output.append(" belt-routes=").append(std::to_string(Value(Metrics.beltRoutes)));
    output.append(" overflow-routes=").append(std::to_string(Value(Metrics.overflowRoutes)));
    output.append(" route-matches=").append(std::to_string(Value(Metrics.routeMatches)));
    output.append(" route-mismatches=").append(std::to_string(Value(Metrics.routeMismatches)));
    output.append(" inventory-aliases=").append(std::to_string(Value(Metrics.inventoryAliases)));
    output.append(" pickup-successes=").append(std::to_string(Value(Metrics.pickupSuccesses)));
    output.append(" pickup-failures=").append(std::to_string(Value(Metrics.pickupFailures)));
    output.append(" codes=");
    for(std::size_t index=0;index<PotionClasses.size();++index) {
        if(index) output.push_back(',');
        output.append(PotionClasses[index].code).append(":")
            .append(std::to_string(Value(Metrics.seenByCode[index]))).append("/")
            .append(std::to_string(Value(Metrics.selectedByCode[index]))).append("/")
            .append(std::to_string(Value(Metrics.pickedByCode[index])));
    }
    output.append(" (seen/selected/picked).");
    return output;
}
std::uint32_t ReadUnitId(void* unit) noexcept {
    __try {
        return unit && UnitId ? UnitId(unit) : RoutingToken::InvalidGuid;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return RoutingToken::InvalidGuid;
    }
}
std::int32_t __fastcall HookGetFreeBeltSlot(
    void* inventory,
    void* item,
    std::int32_t* freeSlot,
    bool allowAnyBeltable) {
    if(Inside) {
        const auto itemGuid=ReadUnitId(item);
        if(ForcedRoute.Matches(itemGuid)) {
            Metrics.routeMatches.fetch_add(1,std::memory_order_relaxed);
            if(inventory!=ForcedInventory) Metrics.inventoryAliases.fetch_add(1,std::memory_order_relaxed);
            if(ForceInventoryOverflow) {
                if(freeSlot) *freeSlot=-1;
                return 0;
            }
            if(freeSlot && ForcedBeltSlot>=0 && ForcedBeltSlot<16) {
                *freeSlot=ForcedBeltSlot;
                return 1;
            }
            return 0;
        }
        Metrics.routeMismatches.fetch_add(1,std::memory_order_relaxed);
    }
    return OriginalGetFreeBeltSlot(inventory,item,freeSlot,allowAnyBeltable);
}

void ResetRoutingScope() noexcept {
    ForceInventoryOverflow=false;
    ForcedBeltSlot=-1;
    ForcedRoute.Reset();
    ForcedInventory=nullptr;
    Inside=false;
}
void ScanUnsafe(void* player) {
    if(!Settings.enabled || Inside || !player) return;
    Metrics.actions.fetch_add(1,std::memory_order_relaxed);
    if((++TriggerCounter%Settings.interval)!=0) return;
    Metrics.scans.fetch_add(1,std::memory_order_relaxed);
    void* game=GetGame(player); if(!game) return;
    void* inventory=GetInventory(player); if(!inventory) return;
    std::array<BeltSlot,16> belt{};
    std::uint8_t beltCapacity{};
    if(!ReadBeltState(inventory,belt,beltCapacity)) {
        Metrics.beltStateFailures.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    void** buckets=nullptr; std::uint32_t count=0; Enumerate(game,&buckets,&count);
    if(!buckets || !count || count>4096) {
        Metrics.enumerationFailures.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    void* best=nullptr; const PotionClass* bestPotion=nullptr; std::int32_t bestDistance=INT_MAX,bestBeltSlot=-1;
    bool bestOverflow{};
    std::uint8_t bestFamilyRank=UINT8_MAX,bestTierRank=UINT8_MAX;
    for(std::uint32_t i=0;i<count;i++) for(void* unit=FirstUnit(buckets[i]);unit;unit=NextUnit(unit)) {
        if(UnitType(unit)!=ItemType || UnitMode(unit)!=GroundMode) continue;
        const auto* potion=ClassifyPackedCode(GetItemCode(unit));
        if(!potion) continue;
        const auto potionIndex=PotionIndex(*potion);
        Metrics.seenByCode[potionIndex].fetch_add(1,std::memory_order_relaxed);
        if(!Accepted(*potion)) {
            Metrics.tierRejects.fetch_add(1,std::memory_order_relaxed);
            continue;
        }
        if(UnitCollision(player,unit,PickupCollisionMask)!=0) {
            Metrics.collisionRejects.fetch_add(1,std::memory_order_relaxed);
            continue;
        }
        const auto distance=UnitDistance(player,unit);
        if(distance<0 || static_cast<std::uint32_t>(distance)>Settings.distance) {
            Metrics.distanceRejects.fetch_add(1,std::memory_order_relaxed);
            continue;
        }
        const auto& family=FamilySettings(potion->family);
        const Item item{potion->code,potion->family,potion->tier};
        const auto beltSlot=ChooseBeltSlot(family.policy,item,belt,beltCapacity);
        const bool overflow=beltSlot<0 && family.policy.AllowsOverflow(item);
        if(beltSlot<0 && !overflow) {
            Metrics.destinationRejects.fetch_add(1,std::memory_order_relaxed);
            continue;
        }
        const auto familyRank=FamilyRank(potion->family),tierRank=TierRank(family,potion->tier);
        const bool better=!best
            || familyRank<bestFamilyRank
            || (familyRank==bestFamilyRank && tierRank<bestTierRank)
            || (familyRank==bestFamilyRank && tierRank==bestTierRank && distance<bestDistance);
        if(better) {
            best=unit; bestPotion=potion; bestDistance=distance; bestBeltSlot=beltSlot;
            bestOverflow=overflow; bestFamilyRank=familyRank; bestTierRank=tierRank;
        }
    }
    if(!best || !bestPotion) return;
    const auto bestGuid=ReadUnitId(best);
    if(bestGuid==RoutingToken::InvalidGuid) return;
    const auto bestIndex=PotionIndex(*bestPotion);
    Metrics.selections.fetch_add(1,std::memory_order_relaxed);
    Metrics.selectedByCode[bestIndex].fetch_add(1,std::memory_order_relaxed);
    if(bestOverflow) Metrics.overflowRoutes.fetch_add(1,std::memory_order_relaxed);
    else Metrics.beltRoutes.fetch_add(1,std::memory_order_relaxed);
    Inside=true; ForcedInventory=inventory; ForcedRoute.itemGuid=bestGuid;
    ForcedBeltSlot=bestBeltSlot; ForceInventoryOverflow=bestOverflow;
    // Same server pickup routine and flags used by vanilla automatic gold pickup.
    const bool picked=Pickup(player,bestGuid,true,Settings.distance,true,false);
    if(picked) {
        Metrics.pickupSuccesses.fetch_add(1,std::memory_order_relaxed);
        Metrics.pickedByCode[bestIndex].fetch_add(1,std::memory_order_relaxed);
    } else Metrics.pickupFailures.fetch_add(1,std::memory_order_relaxed);
    ResetRoutingScope();
}

std::uint32_t ScanProtected(void* player) noexcept {
    __try {
        ScanUnsafe(player);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ResetRoutingScope();
        return GetExceptionCode();
    }
}
void Scan(void* player) {
    const auto exception=ScanProtected(player);
    if(exception && !LoggedScanException && Context) {
        LoggedScanException=true;
        Context->LogWarn("PotionAutoPickup: one automatic scan was skipped after a structured exception.");
    }
}
std::int64_t __fastcall HookTrigger(void* game,void* player,void* packet,std::int32_t size) {
    const auto opcode=packet && size>0
        ? *static_cast<const std::uint8_t*>(packet)
        : static_cast<std::uint8_t>(0);
    if(opcode<FirstTriggerOpcode || opcode>LastTriggerOpcode || !OriginalTriggers[opcode]) return 1;
    const auto result=OriginalTriggers[opcode](game,player,packet,size);
    Scan(player);
    return result;
}
auto Status(D2R::Game::Client*,const D2RL::ConsoleCommandContext* command,void*) noexcept -> D2RL::ConsoleCommandResult {
    if(!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    const auto summary=Summary(); command->plugin->WriteConsoleMessage(summary.c_str());
    const auto metrics=MetricsSummary(); command->plugin->WriteConsoleMessage(metrics.c_str());
    return D2RL::ConsoleCommandResult::Handled;
}
template<class T> T At(std::uintptr_t rva) { return reinterpret_cast<T>(Base+rva); }
bool ValidateTriggerTable() {
    for(std::uint8_t opcode=FirstTriggerOpcode;opcode<=LastTriggerOpcode;++opcode) {
        const auto expected=reinterpret_cast<std::uintptr_t>(Base+TriggerHandlerRvas[opcode]);
        if(!Context->CheckExpectedBytes(
                ServerPacketTableRva+static_cast<std::uintptr_t>(opcode)*sizeof(std::uintptr_t),
                &expected,
                static_cast<std::uint32_t>(sizeof(expected)))) return false;
    }
    return true;
}
bool InstallTriggerTablePatches() {
    const auto replacement=static_cast<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(&HookTrigger));
    for(std::uint8_t opcode=FirstTriggerOpcode;opcode<=LastTriggerOpcode;++opcode) {
        const auto expected=reinterpret_cast<std::uintptr_t>(Base+TriggerHandlerRvas[opcode]);
        OriginalTriggers[opcode]=reinterpret_cast<TriggerFn>(expected);
        if(!PSh_ManifestPatchBytes(
                Context,
                TriggerManifestIds[opcode],
                ServerPacketTableRva+static_cast<std::uintptr_t>(opcode)*sizeof(std::uintptr_t),
                &expected,
                static_cast<std::uint32_t>(sizeof(expected)),
                &replacement,
                static_cast<std::uint32_t>(sizeof(replacement)))) return false;
    }
    return true;
}
}

bool RuffnecKk::PotionAutoPickUp::Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& itemsConfig) noexcept {
    if(!context) return false;
    Context=context;
    Base=reinterpret_cast<std::uint8_t*>(context->exeBase);
    if(!Base) return false;
    try {
        Settings=ParseConfig(itemsConfig);
    } catch(const std::exception& exception) {
        const auto message=std::string(
            "plugin-items: invalid items.potionAutoPickUp (")
            +exception.what()+").";
        context->LogError(message.c_str());
        return false;
    }
    if(context->modDataVersionBuild!=0 && context->modDataVersionBuild!=SupportedBuild) {
        context->LogError("plugin-items: PotionAutoPickUp supports only D2R build 92777.");
        return false;
    }
    if(!PSh_RegisterConsoleCommand(
            context,
            "potion-auto-pick-up",
            Status,
            "Show PotionAutoPickUp configuration and routing counters.")) {
        context->LogWarn(
            "plugin-items: PotionAutoPickUp status command was not registered.");
    }
    if(!Settings.enabled) {
        context->LogInfo(
            "plugin-items: PotionAutoPickUp 1.1.3 by RuffnecKk disabled; "
            "config=items.potionAutoPickUp; no hooks installed.");
        return true;
    }

    GetGame=At<GetGameFn>(GetGameRva); Enumerate=At<EnumerateFn>(EnumerateRva); FirstUnit=At<UnitFn>(FirstUnitRva); NextUnit=At<UnitFn>(NextUnitRva);
    UnitType=At<UnitIntFn>(UnitTypeRva); UnitId=At<UnitIntFn>(UnitIdRva); UnitMode=At<UnitIntFn>(UnitModeRva); UnitDistance=At<UnitPairFn>(UnitDistanceRva);
    UnitCollision=At<CollisionFn>(UnitCollisionRva); Pickup=At<PickupFn>(PickupRva);
    GetItemCode=At<GetItemCodeFn>(GetItemCodeRva);
    GetInventory=At<GetInventoryFn>(GetInventoryRva);
    GetBeltType=At<GetBeltTypeFn>(GetBeltTypeRva);
    ResolveOccupancyGrid=At<ResolveOccupancyGridFn>(ResolveOccupancyGridRva);

    if(!ValidateTriggerTable()
        || !context->CheckExpectedBytes(GetFreeBeltSlotRva,GetFreeBeltSlotExpected.data(),static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()))
        || !context->CheckExpectedBytes(GetItemCodeRva,GetItemCodeExpected.data(),static_cast<std::uint32_t>(GetItemCodeExpected.size()))
        || !context->CheckExpectedBytes(ResolveOccupancyGridRva,ResolveOccupancyGridExpected.data(),static_cast<std::uint32_t>(ResolveOccupancyGridExpected.size()))
        || !context->CheckExpectedBytes(GetInventoryRva,GetInventoryExpected.data(),static_cast<std::uint32_t>(GetInventoryExpected.size()))
        || !context->CheckExpectedBytes(GetBeltTypeRva,GetBeltTypeExpected.data(),static_cast<std::uint32_t>(GetBeltTypeExpected.size()))) {
        context->LogError("plugin-items: PotionAutoPickUp runtime signature mismatch; hooks refused.");
        return false;
    }
    if(!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("items.potionAutoPickUp.getFreeBeltSlot"),
            GetFreeBeltSlotRva,
            GetFreeBeltSlotExpected.data(),
            static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()),
            HookGetFreeBeltSlot,
            &OriginalGetFreeBeltSlot)) {
        context->LogError("plugin-items: PotionAutoPickUp free-belt-slot hook installation failed.");
        return false;
    }
    if(!InstallTriggerTablePatches()) {
        context->LogError("plugin-items: PotionAutoPickUp player-action trigger table patch failed.");
        return false;
    }
    const auto summary=Summary();
    context->LogInfo(summary.c_str());
    return true;
}
void RuffnecKk::PotionAutoPickUp::Unload() noexcept {
    ResetRoutingScope();
    OriginalTriggers.fill(nullptr);
    OriginalGetFreeBeltSlot=nullptr;
    GetGame=nullptr;
    Enumerate=nullptr;
    FirstUnit=nullptr;
    NextUnit=nullptr;
    UnitType=nullptr;
    UnitId=nullptr;
    UnitMode=nullptr;
    UnitDistance=nullptr;
    UnitCollision=nullptr;
    Pickup=nullptr;
    GetItemCode=nullptr;
    GetInventory=nullptr;
    GetBeltType=nullptr;
    ResolveOccupancyGrid=nullptr;
    Settings={};
    Base=nullptr;
    Context=nullptr;
}
