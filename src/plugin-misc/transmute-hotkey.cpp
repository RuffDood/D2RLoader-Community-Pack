#include "transmute-hotkey.h"
#include <plugin-shared.h>
#include "transmute-hotkey-policy.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace RuffnecKk::TransmuteHotkey {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uint64_t RequestLifetimeMilliseconds = 250;
constexpr WPARAM DispatchRequestCookie = 0x544D484B; // TMHK

constexpr std::uintptr_t IntegratedCubeUpdateRva = 0x23ECD0;
constexpr std::uintptr_t StandaloneCubeUpdateRva = 0x2CDA90;
constexpr std::uintptr_t DispatchUiMessageRva = 0x843D90;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x846170;
constexpr std::uintptr_t FindWidgetRva = 0x856220;

constexpr std::size_t IntegratedPanelOffset = 0x2A0;
constexpr std::size_t WidgetEnabledOffset = 0x50;
constexpr std::size_t WidgetVisibleOffset = 0x51;
constexpr std::size_t ButtonOnClickMessageOffset = 0x558;
constexpr std::size_t UiMessageEventOffset = 0x88;

constexpr std::array<std::uint8_t, 32> IntegratedCubeUpdateExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B,
    0xF9, 0x48, 0x8B, 0x89, 0xA0, 0x02, 0x00, 0x00,
    0x48, 0x85, 0xC9, 0x0F, 0x84, 0xA2, 0x01, 0x00,
    0x00, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x48, 0x8D
};
constexpr std::array<std::uint8_t, 32> StandaloneCubeUpdateExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x3D,
    0x97, 0xC6, 0x7C, 0x02, 0x00, 0x48, 0x8B, 0xF9,
    0x48, 0x89, 0x5C, 0x24, 0x30, 0x48, 0x89, 0x74,
    0x24, 0x40, 0x74, 0x0E, 0xB2, 0x01, 0xC6, 0x05
};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
    0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
    0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
    0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 32> FindWidgetExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B
};

using PanelUpdateFn = void(__fastcall*)(void* owner) noexcept;
using DispatchUiMessageFn = void(__fastcall*)(void* message) noexcept;
using FindTopLevelPanelFn = void*(__fastcall*)(const char* name) noexcept;
using FindWidgetFn = void*(__fastcall*)(void* panel, const char* name) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
PanelUpdateFn OriginalIntegratedCubeUpdate{};
PanelUpdateFn OriginalStandaloneCubeUpdate{};
DispatchUiMessageFn DispatchUiMessage{};
FindTopLevelPanelFn FindTopLevelPanel{};
FindWidgetFn FindWidget{};

HANDLE InputThread{};
DWORD InputThreadId{};
std::atomic_bool InputThreadReady{};
std::atomic_bool InputThreadFailed{};
std::atomic_bool InputStopping{};
std::atomic_bool UiDispatchReady{};
std::atomic_bool HotkeyPressed{};
std::atomic_bool HotkeyCaptured{};
std::atomic<void*> IntegratedCubePanel{};
std::atomic<void*> StandaloneCubePanel{};
std::atomic<std::uint64_t> RequestedAt{};
std::atomic<std::uint64_t> AcceptedRequests{};
std::atomic<std::uint64_t> DispatchedRequests{};
std::atomic<std::uint64_t> RefusedRequests{};
std::atomic<std::uint64_t> StaleRequests{};
std::atomic<std::uint64_t> FailedRequests{};
std::atomic_bool FirstDispatchReported{};
std::atomic_bool UiReadyReported{};
HHOOK UiMessageHookHandle{};
DWORD UiThreadId{};
UINT DispatchRequestMessage{};

void ReplayRefusedHotkey() noexcept;

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

bool ValidateRuntime() noexcept {
    // DispatchUiMessage is owned by plugin-skills and is deliberately called
    // through its composable hook rather than revalidating original bytes.
    return Context->CheckExpectedBytes(
            IntegratedCubeUpdateRva,
            IntegratedCubeUpdateExpected.data(),
            IntegratedCubeUpdateExpected.size())
        && Context->CheckExpectedBytes(
            StandaloneCubeUpdateRva,
            StandaloneCubeUpdateExpected.data(),
            StandaloneCubeUpdateExpected.size())
        && Context->CheckExpectedBytes(
            FindTopLevelPanelRva,
            FindTopLevelPanelExpected.data(),
            FindTopLevelPanelExpected.size())
        && Context->CheckExpectedBytes(
            FindWidgetRva,
            FindWidgetExpected.data(),
            FindWidgetExpected.size());
}

void* IntegratedPanel(void* controller) noexcept {
    if (!controller) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(controller) + IntegratedPanelOffset
        );
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* FindNamedWidget(void* panel, const char* name) noexcept {
    if (!panel || !name) return nullptr;
    __try {
        return FindWidget(panel, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool WidgetIsVisible(void* widget) noexcept {
    if (!widget) return false;
    __try {
        return *(static_cast<std::uint8_t*>(widget) + WidgetVisibleOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ButtonIsUsable(void* panel, void* button) noexcept {
    if (!panel || !button) return false;
    __try {
        if (!WidgetIsVisible(panel)
            || *(static_cast<std::uint8_t*>(button) + WidgetEnabledOffset) == 0
            || *(static_cast<std::uint8_t*>(button) + WidgetVisibleOffset) == 0) {
            return false;
        }
        auto* message = static_cast<std::uint8_t*>(button)
            + ButtonOnClickMessageOffset;
        const auto event = *reinterpret_cast<const std::uint64_t*>(
            message + UiMessageEventOffset
        );
        return event != 0 && FindWidget(panel, "convert") == button;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool KnownInputIsBlocked() noexcept {
    constexpr const char* blockers[]{
        "ChatPanel",
        "TextInputModal",
        "DropGoldModal",
        "ConfirmationModal",
        "ButtonBindingModal",
        "KeyBindingDefaultsModal",
        "AddFriendModal",
        "LootFilterRenameProfileModal",
        "LootFilterExportProfileModal",
        "LootFilterDeleteProfileModal",
        "LootFilterNewProfileModal",
        "LootFilterImportProfileModal",
        "LootFilterRenameRuleModal",
        "LootFilterCopyRuleModal",
        "LootFilterDeleteRuleModal",
    };
    __try {
        for (const auto* name : blockers) {
            if (WidgetIsVisible(FindTopLevelPanel(name))) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return false;
}

void ObserveCubePanel(std::atomic<void*>& slot, void* panel) noexcept {
    auto* button = FindNamedWidget(panel, "convert");
    slot.store(
        ButtonIsUsable(panel, button) ? panel : nullptr,
        std::memory_order_release
    );
}

bool TryDispatchPanel(
    std::atomic<void*>& slot,
    const char* surface
) noexcept {
    auto* panel = slot.load(std::memory_order_acquire);
    if (!panel) return false;
    auto* button = FindNamedWidget(panel, "convert");
    if (!ButtonIsUsable(panel, button)) {
        slot.store(nullptr, std::memory_order_release);
        return false;
    }

    __try {
        auto* message = static_cast<std::uint8_t*>(button)
            + ButtonOnClickMessageOffset;
        DispatchUiMessage(message);
        const auto count = DispatchedRequests.fetch_add(
            1,
            std::memory_order_relaxed
        ) + 1;
        if (Context && !FirstDispatchReported.exchange(
                true,
                std::memory_order_relaxed
            )) {
            char log[240]{};
            std::snprintf(
                log,
                sizeof(log),
                "TransmuteHotkey: first native convert message dispatched on %s surface (count=%llu).",
                surface,
                static_cast<unsigned long long>(count)
            );
            Context->LogInfo(log);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        FailedRequests.fetch_add(1, std::memory_order_relaxed);
        slot.store(nullptr, std::memory_order_release);
        if (Context) {
            Context->LogError("TransmuteHotkey: native convert message dispatch failed.");
        }
        return false;
    }
}

void ProcessQueuedRequest() noexcept {
    const auto requestedAt = RequestedAt.exchange(0, std::memory_order_acq_rel);
    if (requestedAt == 0) return;
    const auto now = GetTickCount64();
    if (!IsFreshRequest(now, requestedAt, RequestLifetimeMilliseconds)) {
        StaleRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    if (KnownInputIsBlocked()) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    if (TryDispatchPanel(StandaloneCubePanel, "standalone")
        || TryDispatchPanel(IntegratedCubePanel, "integrated")) {
        return;
    }
    RefusedRequests.fetch_add(1, std::memory_order_relaxed);
    ReplayRefusedHotkey();
}

void __fastcall HookIntegratedCubeUpdate(void* controller) noexcept {
    OriginalIntegratedCubeUpdate(controller);
    ObserveCubePanel(IntegratedCubePanel, IntegratedPanel(controller));
}

void __fastcall HookStandaloneCubeUpdate(void* panel) noexcept {
    OriginalStandaloneCubeUpdate(panel);
    ObserveCubePanel(StandaloneCubePanel, panel);
}

bool CurrentProcessOwnsForegroundWindow() noexcept {
    const auto foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId{};
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

HWND FindGameWindow() noexcept {
    struct Search {
        DWORD processId{};
        HWND window{};
        std::int64_t area{};
    } search{GetCurrentProcessId()};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        DWORD processId{};
        GetWindowThreadProcessId(window, &processId);
        if (processId != state.processId || !IsWindowVisible(window)
            || GetWindow(window, GW_OWNER) != nullptr) {
            return TRUE;
        }
        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;
        const auto area = static_cast<std::int64_t>(client.right - client.left)
            * static_cast<std::int64_t>(client.bottom - client.top);
        if (area > state.area) {
            state.window = window;
            state.area = area;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

bool ModifierDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

void ReplayRefusedHotkey() noexcept {
    std::array<INPUT, 2> input{};
    if (!IsMouseHotkey(Settings.hotkey)) {
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = static_cast<WORD>(Settings.hotkey.virtualKey);
        input[1] = input[0];
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    } else {
        input[0].type = INPUT_MOUSE;
        input[1].type = INPUT_MOUSE;
        switch (Settings.hotkey.virtualKey) {
        case VK_MBUTTON:
            input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
        case VK_XBUTTON1:
        case VK_XBUTTON2:
            input[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_XUP;
            input[0].mi.mouseData = Settings.hotkey.virtualKey == VK_XBUTTON1
                ? XBUTTON1 : XBUTTON2;
            input[1].mi.mouseData = input[0].mi.mouseData;
            break;
        default:
            return;
        }
    }
    // Injected transitions bypass capture, so a request refused by the UI
    // thread is replayed exactly once as D2R's normal key or mouse action.
    if (SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT))
            != input.size() && Context) {
        Context->LogWarn(
            "TransmuteHotkey: a refused hotkey could not be replayed to the game.");
    }
}

bool CubePanelObserved() noexcept {
    return StandaloneCubePanel.load(std::memory_order_acquire) != nullptr
        || IntegratedCubePanel.load(std::memory_order_acquire) != nullptr;
}

void ResetUiMessageHook() noexcept {
    UiDispatchReady.store(false, std::memory_order_release);
    if (UiMessageHookHandle) UnhookWindowsHookEx(UiMessageHookHandle);
    UiMessageHookHandle = nullptr;
    UiThreadId = 0;
}

LRESULT CALLBACK UiMessageHook(
    int code,
    WPARAM removeMode,
    LPARAM parameter
) noexcept {
    if (code >= 0 && removeMode == PM_REMOVE && parameter) {
        auto* message = reinterpret_cast<MSG*>(parameter);
        if (message->message == DispatchRequestMessage
            && message->wParam == DispatchRequestCookie) {
            message->message = WM_NULL;
            message->wParam = 0;
            message->lParam = 0;
            ProcessQueuedRequest();
        }
    }
    return CallNextHookEx(UiMessageHookHandle, code, removeMode, parameter);
}

bool EnsureUiMessageHook(HMODULE module) noexcept {
    if (UiMessageHookHandle) return true;
    const auto window = FindGameWindow();
    if (!window || DispatchRequestMessage == 0) return false;
    const auto threadId = GetWindowThreadProcessId(window, nullptr);
    if (threadId == 0) return false;
    const auto hook = SetWindowsHookExW(
        WH_GETMESSAGE,
        UiMessageHook,
        module,
        threadId
    );
    if (!hook) return false;
    UiMessageHookHandle = hook;
    UiThreadId = threadId;
    UiDispatchReady.store(true, std::memory_order_release);
    if (Context && !UiReadyReported.exchange(true, std::memory_order_relaxed)) {
        char log[180]{};
        std::snprintf(
            log,
            sizeof(log),
            "TransmuteHotkey: UI-thread handoff ready on thread %lu for %s input.",
            static_cast<unsigned long>(threadId),
            IsMouseHotkey(Settings.hotkey) ? "mouse" : "keyboard"
        );
        Context->LogInfo(log);
    }
    return true;
}

bool QueueInputRequest() noexcept {
    if (!CurrentProcessOwnsForegroundWindow()
        || !UiDispatchReady.load(std::memory_order_acquire)
        || !CubePanelObserved()) {
        return false;
    }
    if (!ExactModifiersMatch(
            Settings.hotkey,
            ModifierDown(VK_CONTROL),
            ModifierDown(VK_SHIFT),
            ModifierDown(VK_MENU)
        )) {
        return false;
    }

    auto now = GetTickCount64();
    if (now == 0) now = 1;
    std::uint64_t noPendingRequest{};
    if (!RequestedAt.compare_exchange_strong(
            noPendingRequest,
            now,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        // A second press is not captured while an earlier request still owns
        // the handoff; it continues to D2R through the normal input path.
        return false;
    }
    if (!PostThreadMessageW(
            UiThreadId,
            DispatchRequestMessage,
            DispatchRequestCookie,
            0
        )) {
        RequestedAt.store(0, std::memory_order_release);
        FailedRequests.fetch_add(1, std::memory_order_relaxed);
        ResetUiMessageHook();
        return false;
    }
    AcceptedRequests.fetch_add(1, std::memory_order_relaxed);
    HotkeyCaptured.store(true, std::memory_order_release);
    return true;
}

bool HandleInputTransition(
    bool isDown,
    bool isUp,
    bool injected
) noexcept {
    if (InputStopping.load(std::memory_order_acquire)) return false;
    // Replayed input must pass through untouched. In particular, do not let
    // its injected key-up clear the capture that owns the physical key-up.
    if (injected) return false;
    if (isUp) {
        HotkeyPressed.store(false, std::memory_order_release);
        const auto captured = HotkeyCaptured.exchange(
            false,
            std::memory_order_acq_rel
        );
        return captured && CurrentProcessOwnsForegroundWindow();
    }
    if (!isDown || !CurrentProcessOwnsForegroundWindow()) return false;

    const auto firstDown = !HotkeyPressed.exchange(
        true,
        std::memory_order_acq_rel
    );
    if (!firstDown) {
        return HotkeyCaptured.load(std::memory_order_acquire);
    }
    // Capture only when the request is applicable now: the game owns focus,
    // the configured modifiers match, the UI handoff is ready, and a usable
    // Cube Transmute panel has been observed. Otherwise the key reaches D2R.
    return QueueInputRequest();
}

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
    if (IsMouseHotkey(Settings.hotkey)
        || input->vkCode != Settings.hotkey.virtualKey) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }

    const auto isDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto isUp = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!isDown && !isUp) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLKHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

LRESULT CALLBACK MouseHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter || !IsMouseHotkey(Settings.hotkey)) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
    bool matches{};
    bool isDown{};
    bool isUp{};
    switch (Settings.hotkey.virtualKey) {
    case VK_MBUTTON:
        matches = message == WM_MBUTTONDOWN || message == WM_MBUTTONUP;
        isDown = message == WM_MBUTTONDOWN;
        isUp = message == WM_MBUTTONUP;
        break;
    case VK_XBUTTON1:
    case VK_XBUTTON2: {
        const auto expected = Settings.hotkey.virtualKey == VK_XBUTTON1
            ? XBUTTON1
            : XBUTTON2;
        matches = (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
            && HIWORD(input->mouseData) == expected;
        isDown = message == WM_XBUTTONDOWN;
        isUp = message == WM_XBUTTONUP;
        break;
    }
    default:
        break;
    }
    if (!matches) return CallNextHookEx(nullptr, code, message, parameter);
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLMHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

DWORD WINAPI InputThreadProc(void* parameter) noexcept {
    const auto module = static_cast<HMODULE>(parameter);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    DispatchRequestMessage = RegisterWindowMessageW(
        L"RuffnecKk.TransmuteHotkey.Dispatch"
    );
    const auto inputHook = SetWindowsHookExW(
        IsMouseHotkey(Settings.hotkey) ? WH_MOUSE_LL : WH_KEYBOARD_LL,
        IsMouseHotkey(Settings.hotkey) ? MouseHook : KeyboardHook,
        module,
        0
    );
    const auto uiHookTimer = SetTimer(nullptr, 0, 100, nullptr);
    if (!inputHook || !uiHookTimer || DispatchRequestMessage == 0) {
        if (uiHookTimer) KillTimer(nullptr, uiHookTimer);
        if (inputHook) UnhookWindowsHookEx(inputHook);
        InputThreadFailed.store(true, std::memory_order_release);
        InputThreadReady.store(true, std::memory_order_release);
        FreeLibraryAndExitThread(module, 1);
    }

    EnsureUiMessageHook(module);
    InputThreadReady.store(true, std::memory_order_release);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.wParam == uiHookTimer
            && !UiDispatchReady.load(std::memory_order_acquire)) {
            EnsureUiMessageHook(module);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    KillTimer(nullptr, uiHookTimer);
    ResetUiMessageHook();
    UnhookWindowsHookEx(inputHook);
    FreeLibraryAndExitThread(module, 0);
}

bool StartInput() noexcept {
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_release);
    HMODULE workerModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&InputThreadProc),
            &workerModule)) {
        return false;
    }
    InputThread = CreateThread(
        nullptr,
        0,
        InputThreadProc,
        workerModule,
        0,
        &InputThreadId
    );
    if (!InputThread) {
        FreeLibrary(workerModule);
        return false;
    }
    for (unsigned attempt = 0;
         attempt < 200 && !InputThreadReady.load(std::memory_order_acquire);
         ++attempt) {
        Sleep(10);
    }
    return InputThreadReady.load(std::memory_order_acquire)
        && !InputThreadFailed.load(std::memory_order_acquire);
}

bool StopInput() noexcept {
    InputStopping.store(true, std::memory_order_release);
    if (InputThreadId != 0) PostThreadMessageW(InputThreadId, WM_QUIT, 0, 0);
    if (InputThread) {
        const auto wait = WaitForSingleObject(InputThread, 3000);
        if (wait != WAIT_OBJECT_0) {
            if (Context) {
                Context->LogError(
                    "plugin-misc: Transmute Hotkey input worker did not stop; its module reference is retained for safety.");
            }
            return false;
        }
        CloseHandle(InputThread);
    }
    InputThread = nullptr;
    InputThreadId = 0;
    DispatchRequestMessage = 0;
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[520]{};
    std::snprintf(
        message,
        sizeof(message),
        "Transmute Hotkey 0.2.0: enabled=%s; hotkey=%s; input=%s; UI dispatch=%s; config=misc.transmuteHotkey; accepted=%llu; dispatched=%llu; refused=%llu; stale=%llu; failed=%llu.",
        Settings.enabled ? "true" : "false",
        Settings.hotkeyText.c_str(),
        IsMouseHotkey(Settings.hotkey) ? "mouse" : "keyboard",
        UiDispatchReady.load(std::memory_order_acquire) ? "ready" : "pending",
        static_cast<unsigned long long>(AcceptedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(DispatchedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RefusedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(StaleRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(FailedRequests.load(std::memory_order_relaxed))
    );
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Settings = {};
    OriginalIntegratedCubeUpdate = nullptr;
    OriginalStandaloneCubeUpdate = nullptr;
    DispatchUiMessage = nullptr;
    FindTopLevelPanel = nullptr;
    FindWidget = nullptr;
    RequestedAt.store(0, std::memory_order_relaxed);
    UiDispatchReady.store(false, std::memory_order_relaxed);
    HotkeyPressed.store(false, std::memory_order_relaxed);
    HotkeyCaptured.store(false, std::memory_order_relaxed);
    IntegratedCubePanel.store(nullptr, std::memory_order_relaxed);
    StandaloneCubePanel.store(nullptr, std::memory_order_relaxed);
    AcceptedRequests.store(0, std::memory_order_relaxed);
    DispatchedRequests.store(0, std::memory_order_relaxed);
    RefusedRequests.store(0, std::memory_order_relaxed);
    StaleRequests.store(0, std::memory_order_relaxed);
    FailedRequests.store(0, std::memory_order_relaxed);
    FirstDispatchReported.store(false, std::memory_order_relaxed);
    UiReadyReported.store(false, std::memory_order_relaxed);
    UiMessageHookHandle = nullptr;
    UiThreadId = 0;
    DispatchRequestMessage = 0;

    try {
        Settings = ParseConfig(miscConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-misc: invalid misc.transmuteHotkey (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

	if (!PSh_RegisterConsoleCommand(context,
            "transmute-hotkey",
            Status,
            "Show Transmute hotkey status and counters."
        )) {
        context->LogWarn(
            "plugin-misc: Transmute Hotkey status command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "plugin-misc: Transmute Hotkey 0.2.0 by RuffnecKk disabled; no hooks or input worker installed; config=misc.transmuteHotkey.");
        return true;
    }
    if (!Base) {
        context->LogError("plugin-misc: D2R executable base is unavailable.");
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "plugin-misc: Transmute Hotkey supports only D2R build 92777.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "plugin-misc: Transmute Hotkey panel-update or UI helper signature mismatch; feature refused.");
        return false;
    }

    DispatchUiMessage = At<DispatchUiMessageFn>(DispatchUiMessageRva);
    FindTopLevelPanel = At<FindTopLevelPanelFn>(FindTopLevelPanelRva);
    FindWidget = At<FindWidgetFn>(FindWidgetRva);

    if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("misc.transmuteHotkey.integratedCubeUpdate"),
            IntegratedCubeUpdateRva,
            IntegratedCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(IntegratedCubeUpdateExpected.size()),
            HookIntegratedCubeUpdate,
            &OriginalIntegratedCubeUpdate
        )) {
        context->LogError(
            "plugin-misc: Transmute Hotkey integrated Cube hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("misc.transmuteHotkey.standaloneCubeUpdate"),
            StandaloneCubeUpdateRva,
            StandaloneCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(StandaloneCubeUpdateExpected.size()),
            HookStandaloneCubeUpdate,
            &OriginalStandaloneCubeUpdate
        )) {
        context->LogError(
            "plugin-misc: Transmute Hotkey standalone Cube hook failed.");
        return false;
    }
    if (!StartInput()) {
        (void)StopInput();
        context->LogError(
            "plugin-misc: Transmute Hotkey bounded input hook failed.");
        return false;
    }

    char message[420]{};
    std::snprintf(
        message,
        sizeof(message),
        "plugin-misc: Transmute Hotkey 0.2.0 by RuffnecKk active; hotkey=%s; input=%s; standalone=0x%llX; integrated=0x%llX; config=misc.transmuteHotkey.",
        Settings.hotkeyText.c_str(),
        IsMouseHotkey(Settings.hotkey) ? "mouse" : "keyboard",
        static_cast<unsigned long long>(StandaloneCubeUpdateRva),
        static_cast<unsigned long long>(IntegratedCubeUpdateRva));
    context->LogInfo(message);
    return true;
}

void Unload() noexcept {
    if (!StopInput()) return;
    RequestedAt.store(0, std::memory_order_release);
    IntegratedCubePanel.store(nullptr, std::memory_order_release);
    StandaloneCubePanel.store(nullptr, std::memory_order_release);
    OriginalIntegratedCubeUpdate = nullptr;
    OriginalStandaloneCubeUpdate = nullptr;
    DispatchUiMessage = nullptr;
    FindTopLevelPanel = nullptr;
    FindWidget = nullptr;
    Settings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::TransmuteHotkey
