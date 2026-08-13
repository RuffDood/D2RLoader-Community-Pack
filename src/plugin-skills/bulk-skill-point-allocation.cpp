#include <D2RLPlugin/api.h>
#include "bulk-skill-point-allocation.h"
#include <plugin-shared.h>
#include "bulk-skill-point-allocation-policy.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace RuffnecKk::BulkSkillPointAllocation {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t SendFiveBytePacketRva = 0x0EC700;
constexpr std::uintptr_t IsVirtualKeyDownRva = 0x120A100;
constexpr std::uintptr_t GetLocalizedStringByKeyRva = 0x5F4B90;
constexpr std::uintptr_t ShowAssignAllStatsConfirmationRva = 0x14EF670;
constexpr std::uintptr_t UiDispatchMessageRva = 0x843D90;
constexpr std::uint8_t AllocateSkillOpcode = 0x3B;
constexpr std::int32_t SkillConfirmationSentinel = 0x42534B50;
constexpr std::size_t FakeStatWidgetSize = 0xB90;
constexpr std::size_t FakeStatIndexOffset = 0xB88;
constexpr std::size_t MessagePayloadOffset = 0x110;
constexpr char AssignAllStatPointsConfirmationKey[] = "AssignAllStatPointsConfirmation";
constexpr char MissingStringKey[] = "strMissingString";
constexpr wchar_t PackRemoteStashModule[] = L"plugin-misc.dll";
constexpr char PackRemoteStashInterceptorExport[] =
    "RuffneckkRemoteStashInterceptUiMessage";
struct GameStringView {
    const char* data{};
    std::size_t size{};
};

struct PendingConfirmationState {
    bool active{};
    std::uint16_t skillId{};
};

using SendFiveBytePacketFn = void(__fastcall*)(std::uint8_t, std::uint16_t, std::uint16_t) noexcept;
using IsVirtualKeyDownFn = std::uint32_t(__fastcall*)(std::int32_t) noexcept;
using GetLocalizedStringByKeyFn = const char*(__fastcall*)(const GameStringView*) noexcept;
using ShowAssignAllStatsConfirmationFn = void(__fastcall*)(const void*) noexcept;
using UiDispatchMessageFn = void(__fastcall*)(void*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Policy Settings{};
SendFiveBytePacketFn OriginalSendFiveBytePacket{};
IsVirtualKeyDownFn IsVirtualKeyDown{};
GetLocalizedStringByKeyFn OriginalGetLocalizedStringByKey{};
ShowAssignAllStatsConfirmationFn ShowAssignAllStatsConfirmation{};
UiDispatchMessageFn OriginalUiDispatchMessage{};
std::atomic<UiMessageInterceptorFn> ExternalUiMessageInterceptor{};
std::atomic_bool BrokerReady{};
UiMessageInterceptorFn PackRemoteStashInterceptor{};

std::mutex ConfirmationMutex;
PendingConfirmationState PendingConfirmation{};
alignas(16) std::array<std::uint8_t, FakeStatWidgetSize> FakeStatWidget{};
thread_local bool OpeningSkillConfirmation{};

std::atomic<std::uint64_t> SingleClicks{};
std::atomic<std::uint64_t> CtrlBatches{};
std::atomic<std::uint64_t> ShiftAccepted{};
std::atomic<std::uint64_t> ShiftCancelled{};
std::atomic<std::uint64_t> NativeBulkPacketsSent{};
std::atomic<std::uint32_t> LastModifierMask{};
std::atomic<std::uint16_t> LastIncomingExtra{};
std::atomic<std::uint16_t> LastOutgoingExtra{};

enum ModifierMask : std::uint32_t {
    NativeCtrl = 1U << 0,
    NativeLeftCtrl = 1U << 1,
    NativeRightCtrl = 1U << 2,
    Win32Ctrl = 1U << 3,
    Win32LeftCtrl = 1U << 4,
    Win32RightCtrl = 1U << 5,
    PacketShift = 1U << 6,
};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

const char* __fastcall HookGetLocalizedStringByKey(
    const GameStringView* key
) noexcept {
    constexpr std::size_t expectedLength = sizeof(AssignAllStatPointsConfirmationKey) - 1;
    if (OpeningSkillConfirmation
        && key
        && key->data
        && key->size == expectedLength
        && std::memcmp(
            key->data,
            AssignAllStatPointsConfirmationKey,
            expectedLength
        ) == 0) {
        const GameStringView localizedKey{
            .data = Settings.shiftConfirmationKey.data(),
            .size = Settings.shiftConfirmationKey.size(),
        };
        const GameStringView missingStringKey{
            .data = MissingStringKey,
            .size = sizeof(MissingStringKey) - 1,
        };
        const auto localized = OriginalGetLocalizedStringByKey(&localizedKey);
        const auto localizedMissingString =
            OriginalGetLocalizedStringByKey(&missingStringKey);
        if (IsUsableLocalizedString(
                localized,
                Settings.shiftConfirmationKey.c_str(),
                localizedMissingString
            )) {
            return localized;
        }
        return Settings.shiftConfirmationFallback.c_str();
    }
    return OriginalGetLocalizedStringByKey(key);
}

bool CancelPendingConfirmation() noexcept {
    std::scoped_lock lock(ConfirmationMutex);
    if (!PendingConfirmation.active) return false;
    PendingConfirmation = {};
    return true;
}

void ShowShiftConfirmation(std::uint16_t skillId) noexcept {
    {
        std::scoped_lock lock(ConfirmationMutex);
        if (PendingConfirmation.active) ++ShiftCancelled;
        PendingConfirmation = {
            .active = true,
            .skillId = skillId,
        };
    }

    std::int32_t sentinel = SkillConfirmationSentinel;
    std::memcpy(
        FakeStatWidget.data() + FakeStatIndexOffset,
        &sentinel,
        sizeof(sentinel)
    );
    OpeningSkillConfirmation = true;
    ShowAssignAllStatsConfirmation(FakeStatWidget.data());
    OpeningSkillConfirmation = false;
}

bool ReadMessagePayload(
    void* message,
    std::int32_t& statIndex,
    std::int32_t& mode
) noexcept {
    if (!message) return false;
    __try {
        const auto payload = *reinterpret_cast<std::uint8_t**>(
            static_cast<std::uint8_t*>(message) + MessagePayloadOffset
        );
        if (!payload) return false;
        std::memcpy(&statIndex, payload, sizeof(statIndex));
        std::memcpy(&mode, payload + sizeof(statIndex), sizeof(mode));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Win32KeyDown(std::int32_t virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0
        || (GetKeyState(virtualKey) & 0x8000) != 0;
}

std::uint32_t ReadModifierMask(std::uint16_t packetExtra) noexcept {
    std::uint32_t mask{};
    if (IsVirtualKeyDown(VK_CONTROL) != 0) mask |= NativeCtrl;
    if (IsVirtualKeyDown(VK_LCONTROL) != 0) mask |= NativeLeftCtrl;
    if (IsVirtualKeyDown(VK_RCONTROL) != 0) mask |= NativeRightCtrl;
    if (Win32KeyDown(VK_CONTROL)) mask |= Win32Ctrl;
    if (Win32KeyDown(VK_LCONTROL)) mask |= Win32LeftCtrl;
    if (Win32KeyDown(VK_RCONTROL)) mask |= Win32RightCtrl;
    if (packetExtra == AssignAllSkillPointsExtra) mask |= PacketShift;
    return mask;
}

void BeginBulkAllocation(
    AllocationMode mode,
    std::uint16_t skillId
) noexcept {
    const auto requested = mode == AllocationMode::ShiftAll
        ? 1U
        : Settings.skillPointsPerCtrlClick;
    const auto nativeBulkExtra = NativeSkillPacketExtra(mode, requested);
    LastOutgoingExtra.store(nativeBulkExtra, std::memory_order_relaxed);
    OriginalSendFiveBytePacket(AllocateSkillOpcode, skillId, nativeBulkExtra);
    ++NativeBulkPacketsSent;
}

bool TryExternalUiMessageInterceptor(void* message) noexcept {
    const auto interceptor = ExternalUiMessageInterceptor.load(std::memory_order_acquire);
    if (!interceptor) return false;
    __try {
        return interceptor(message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool __fastcall InterceptUiMessageChain(void* message) noexcept {
    if (TryExternalUiMessageInterceptor(message)) return true;

    std::int32_t statIndex{};
    std::int32_t mode{};
    const bool payloadRead = ReadMessagePayload(message, statIndex, mode);
    bool confirmationPending{};
    {
        std::scoped_lock lock(ConfirmationMutex);
        confirmationPending = PendingConfirmation.active;
    }
    if (OpeningSkillConfirmation
        || !payloadRead
        || statIndex != SkillConfirmationSentinel) {
        return false;
    }

    PendingConfirmationState pending;
    {
        std::scoped_lock lock(ConfirmationMutex);
        pending = PendingConfirmation;
        PendingConfirmation = {};
    }
    if (!pending.active) return true;

    ++ShiftAccepted;
    BeginBulkAllocation(AllocationMode::ShiftAll, pending.skillId);
    return true;
}

void __fastcall HookUiDispatchMessage(void* message) noexcept {
    if (InterceptUiMessageChain(message)) return;
    OriginalUiDispatchMessage(message);
}

void __fastcall HookSendFiveBytePacket(
    std::uint8_t opcode,
    std::uint16_t value,
    std::uint16_t extra
) noexcept {
    if (opcode != AllocateSkillOpcode) {
        OriginalSendFiveBytePacket(opcode, value, extra);
        return;
    }

    const auto modifierMask = ReadModifierMask(extra);
    LastModifierMask.store(modifierMask, std::memory_order_relaxed);
    LastIncomingExtra.store(extra, std::memory_order_relaxed);
    const bool shiftPressed = (modifierMask & PacketShift) != 0;
    const bool ctrlPressed = (modifierMask & (
        NativeCtrl | NativeLeftCtrl | NativeRightCtrl
        | Win32Ctrl | Win32LeftCtrl | Win32RightCtrl
    )) != 0;
    const auto mode = ResolveMode(shiftPressed, ctrlPressed);
    if (mode == AllocationMode::Single) {
        if (CancelPendingConfirmation()) ++ShiftCancelled;
        ++SingleClicks;
        LastOutgoingExtra.store(extra, std::memory_order_relaxed);
        OriginalSendFiveBytePacket(opcode, value, extra);
        return;
    }

    if (mode == AllocationMode::ShiftAll) {
        if (!Settings.confirmShiftAllocation) {
            if (CancelPendingConfirmation()) ++ShiftCancelled;
            BeginBulkAllocation(mode, value);
            return;
        }
        ShowShiftConfirmation(value);
        return;
    } else {
        if (CancelPendingConfirmation()) ++ShiftCancelled;
        ++CtrlBatches;
    }
    BeginBulkAllocation(mode, value);
}

D2RL::ConsoleCommandResult __cdecl Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

    PendingConfirmationState confirmation;
    {
        std::scoped_lock lock(ConfirmationMutex);
        confirmation = PendingConfirmation;
    }
    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "Bulk Skill Point Allocation 1.2.5 (plugin-skills): enabled=%s; ctrl skill points=%u; shift confirmation=%s; confirmation=%s; localization key=%s; last modifiers=0x%02X; incoming extra=0x%04X; outgoing extra=0x%04X; single=%llu; ctrl batches=%llu; shift confirmed=%llu; shift superseded=%llu; native bulk packets=%llu.",
        Settings.enabled ? "true" : "false",
        Settings.skillPointsPerCtrlClick,
        Settings.confirmShiftAllocation ? "enabled" : "disabled",
        confirmation.active ? "pending" : "idle",
        Settings.shiftConfirmationKey.c_str(),
        static_cast<unsigned>(LastModifierMask.load(std::memory_order_relaxed)),
        static_cast<unsigned>(LastIncomingExtra.load(std::memory_order_relaxed)),
        static_cast<unsigned>(LastOutgoingExtra.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(SingleClicks.load()),
        static_cast<unsigned long long>(CtrlBatches.load()),
        static_cast<unsigned long long>(ShiftAccepted.load()),
        static_cast<unsigned long long>(ShiftCancelled.load()),
        static_cast<unsigned long long>(NativeBulkPacketsSent.load())
    );
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

bool ValidateRuntime(bool validateConfirmationUi) noexcept {
    constexpr std::array<std::uint8_t, 29> sendPacketExpected{
        0x48, 0x83, 0xEC, 0x28, 0x88, 0x4C, 0x24, 0x48,
        0x48, 0x8D, 0x4C, 0x24, 0x48, 0x66, 0x89, 0x54,
        0x24, 0x49, 0xBA, 0x05, 0x00, 0x00, 0x00, 0x66,
        0x44, 0x89, 0x44, 0x24, 0x4B
    };
    constexpr std::array<std::uint8_t, 21> isVirtualKeyDownExpected{
        0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x86, 0x6E,
        0xAA, 0x00, 0xC1, 0xE8, 0x0F, 0x83, 0xE0, 0x01,
        0x48, 0x83, 0xC4, 0x28, 0xC3
    };
    constexpr std::array<std::uint8_t, 29> localizedStringByKeyExpected{
        0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x49, 0x8D,
        0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x05, 0x20, 0x67, 0x3D, 0x02,
        0x48, 0x33, 0xC4, 0x48, 0x89
    };
    constexpr std::array<std::uint8_t, 29> statsConfirmationExpected{
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x55, 0x57, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFC,
        0xFF, 0xFF, 0x48, 0x81, 0xEC
    };
    constexpr std::array<std::uint8_t, 29> uiDispatchExpected{
        0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x4C, 0x89, 0x7C, 0x24, 0x58, 0x4C, 0x8B, 0xF9,
        0xE8, 0x7B, 0x1C, 0xA6, 0x00, 0x0F, 0xB6, 0x90,
        0x18, 0x01, 0x00, 0x00, 0x84
    };
    return Context->CheckExpectedBytes(
            SendFiveBytePacketRva,
            sendPacketExpected.data(),
            sendPacketExpected.size())
        && Context->CheckExpectedBytes(
            IsVirtualKeyDownRva,
            isVirtualKeyDownExpected.data(),
            isVirtualKeyDownExpected.size())
        && (!validateConfirmationUi
            || (Context->CheckExpectedBytes(
                    GetLocalizedStringByKeyRva,
                    localizedStringByKeyExpected.data(),
                    localizedStringByKeyExpected.size())
                && Context->CheckExpectedBytes(
                    ShowAssignAllStatsConfirmationRva,
                    statsConfirmationExpected.data(),
                    statsConfirmationExpected.size())))
        && (!validateConfirmationUi
            || Context->CheckExpectedBytes(
                UiDispatchMessageRva,
                uiDispatchExpected.data(),
                uiDispatchExpected.size()));
}

} // namespace

bool RegisterUiMessageInterceptor(
    UiMessageInterceptorFn interceptor
) noexcept {
    if (!interceptor || !BrokerReady.load(std::memory_order_acquire)) return false;
    UiMessageInterceptorFn expected{};
    return ExternalUiMessageInterceptor.compare_exchange_strong(
        expected,
        interceptor,
        std::memory_order_acq_rel,
        std::memory_order_acquire
    );
}

bool TryRegisterPackRemoteStashInterceptor() noexcept {
    if (!BrokerReady.load(std::memory_order_acquire)) return false;
    const auto module = GetModuleHandleW(PackRemoteStashModule);
    if (!module) return false;
    const auto interceptor = reinterpret_cast<UiMessageInterceptorFn>(
        GetProcAddress(module, PackRemoteStashInterceptorExport)
    );
    if (!interceptor || !RegisterUiMessageInterceptor(interceptor)) return false;
    PackRemoteStashInterceptor = interceptor;
    return true;
}

void UnregisterUiMessageInterceptor(
    UiMessageInterceptorFn interceptor
) noexcept {
    if (!interceptor) return;
    auto expected = interceptor;
    ExternalUiMessageInterceptor.compare_exchange_strong(
        expected,
        nullptr,
        std::memory_order_acq_rel,
        std::memory_order_acquire
    );
}

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& skillsConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ExternalUiMessageInterceptor.store(nullptr, std::memory_order_relaxed);
    BrokerReady.store(false, std::memory_order_relaxed);
    PackRemoteStashInterceptor = nullptr;
    if (!Base) return false;
    if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "plugin-skills: Bulk Skill Point Allocation supports only D2R build 92777.");
        return false;
    }
    try {
        Settings = ParseConfig(skillsConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-skills: invalid skills.bulkSkillPointAllocation (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

    {
        std::scoped_lock lock(ConfirmationMutex);
        PendingConfirmation = {};
    }
    OpeningSkillConfirmation = false;
    SingleClicks.store(0, std::memory_order_relaxed);
    CtrlBatches.store(0, std::memory_order_relaxed);
    ShiftAccepted.store(0, std::memory_order_relaxed);
    ShiftCancelled.store(0, std::memory_order_relaxed);
    NativeBulkPacketsSent.store(0, std::memory_order_relaxed);
    LastModifierMask.store(0, std::memory_order_relaxed);
    LastIncomingExtra.store(0, std::memory_order_relaxed);
    LastOutgoingExtra.store(0, std::memory_order_relaxed);

    if (Settings.enabled) {
        IsVirtualKeyDown = At<IsVirtualKeyDownFn>(IsVirtualKeyDownRva);
        if (Settings.confirmShiftAllocation) {
            ShowAssignAllStatsConfirmation = At<ShowAssignAllStatsConfirmationFn>(
                ShowAssignAllStatsConfirmationRva
            );
        }

        if (!ValidateRuntime(Settings.confirmShiftAllocation)) {
            context->LogError(
                "plugin-skills: Bulk Skill Point Allocation runtime signature mismatch.");
            return false;
        }

        constexpr std::array<std::uint8_t, 29> localizedStringByKeyExpected{
            0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x49, 0x8D,
            0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
            0x00, 0x48, 0x8B, 0x05, 0x20, 0x67, 0x3D, 0x02,
            0x48, 0x33, 0xC4, 0x48, 0x89
        };
        if (Settings.confirmShiftAllocation
            && !PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.bulkSkillPointAllocation.localizedString"),
                GetLocalizedStringByKeyRva,
                localizedStringByKeyExpected.data(),
                static_cast<std::uint32_t>(localizedStringByKeyExpected.size()),
                HookGetLocalizedStringByKey,
                &OriginalGetLocalizedStringByKey
            )) {
            context->LogError(
                "plugin-skills: Bulk Skill Point Allocation localized-string hook failed.");
            return false;
        }

        constexpr std::array<std::uint8_t, 29> uiDispatchExpected{
            0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20,
            0x4C, 0x89, 0x7C, 0x24, 0x58, 0x4C, 0x8B, 0xF9,
            0xE8, 0x7B, 0x1C, 0xA6, 0x00, 0x0F, 0xB6, 0x90,
            0x18, 0x01, 0x00, 0x00, 0x84
        };
        if (Settings.confirmShiftAllocation
            && !PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.bulkSkillPointAllocation.uiDispatch"),
                    UiDispatchMessageRva,
                    uiDispatchExpected.data(),
                    static_cast<std::uint32_t>(uiDispatchExpected.size()),
                    HookUiDispatchMessage,
                    &OriginalUiDispatchMessage
                )) {
            context->LogError(
                "plugin-skills: Bulk Skill Point Allocation UI-dispatch hook failed.");
            return false;
        }

        constexpr std::array<std::uint8_t, 29> sendPacketExpected{
            0x48, 0x83, 0xEC, 0x28, 0x88, 0x4C, 0x24, 0x48,
            0x48, 0x8D, 0x4C, 0x24, 0x48, 0x66, 0x89, 0x54,
            0x24, 0x49, 0xBA, 0x05, 0x00, 0x00, 0x00, 0x66,
            0x44, 0x89, 0x44, 0x24, 0x4B
        };
        if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.bulkSkillPointAllocation.sendPacket"),
                SendFiveBytePacketRva,
                sendPacketExpected.data(),
                static_cast<std::uint32_t>(sendPacketExpected.size()),
                HookSendFiveBytePacket,
                &OriginalSendFiveBytePacket
            )) {
            context->LogError(
                "plugin-skills: Bulk Skill Point Allocation skill-packet hook failed.");
            return false;
        }
        BrokerReady.store(
            Settings.confirmShiftAllocation,
            std::memory_order_release);
        if (TryRegisterPackRemoteStashInterceptor()) {
            context->LogInfo(
                "plugin-skills: registered plugin-misc RemoteStash with the shared UI broker."
            );
        }
    }

	if (!PSh_RegisterConsoleCommand(context,
            "bulk-skill-points",
            Status,
            "Show bulk skill allocation settings and counters."
        )) {
        context->LogWarn(
            "plugin-skills: Bulk Skill Point Allocation status command was not registered.");
    }
    char activeMessage[320]{};
    std::snprintf(
        activeMessage,
        sizeof(activeMessage),
        "plugin-skills: Bulk Skill Point Allocation 1.2.5 by RuffnecKk loaded: "
        "enabled=%s; ctrl=%u; shift confirmation=%s; UI broker=%s; "
        "config=skills.bulkSkillPointAllocation.",
        Settings.enabled ? "true" : "false",
        Settings.skillPointsPerCtrlClick,
        Settings.confirmShiftAllocation ? "enabled" : "disabled",
        !Settings.enabled
            ? "inactive"
            : (!Settings.confirmShiftAllocation
                ? "not-needed"
                : "plugin-skills"));
    context->LogInfo(activeMessage);
    return true;
}

void Unload() noexcept {
    if (PackRemoteStashInterceptor) {
        UnregisterUiMessageInterceptor(PackRemoteStashInterceptor);
        PackRemoteStashInterceptor = nullptr;
    }
    BrokerReady.store(false, std::memory_order_release);
    ExternalUiMessageInterceptor.store(nullptr, std::memory_order_release);
    CancelPendingConfirmation();
    OpeningSkillConfirmation = false;
    OriginalUiDispatchMessage = nullptr;
    ShowAssignAllStatsConfirmation = nullptr;
    OriginalGetLocalizedStringByKey = nullptr;
    IsVirtualKeyDown = nullptr;
    OriginalSendFiveBytePacket = nullptr;
    Settings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::BulkSkillPointAllocation

extern "C" __declspec(dllexport) bool __cdecl RuffneckkRegisterUiMessageInterceptor(
    RuffnecKk::BulkSkillPointAllocation::UiMessageInterceptorFn interceptor
) noexcept {
    return RuffnecKk::BulkSkillPointAllocation::RegisterUiMessageInterceptor(interceptor);
}

extern "C" __declspec(dllexport) void __cdecl RuffneckkUnregisterUiMessageInterceptor(
    RuffnecKk::BulkSkillPointAllocation::UiMessageInterceptorFn interceptor
) noexcept {
    RuffnecKk::BulkSkillPointAllocation::UnregisterUiMessageInterceptor(interceptor);
}
