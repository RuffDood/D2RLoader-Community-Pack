#include "floating-damage-render.h"

#include "floating-damage.h"
#include "floating-damage-resource.h"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace D3D12 {
namespace {
using Microsoft::WRL::ComPtr;

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> renderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

HMODULE Module{};
std::array<void*, 150> Methods{};
PresentFn OriginalPresent{};
ExecuteCommandListsFn OriginalExecuteCommandLists{};
ResizeBuffersFn OriginalResizeBuffers{};

std::mutex RenderMutex;
bool HooksInstalled{};
bool RendererInitialized{};
HWND Window{};
DXGI_FORMAT BackBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};

// D2RLoader terminates the process without always invoking the plugin unload
// callback. Keep GPU references in intentionally process-lifetime storage so
// C++ static destruction cannot release them after D3D12Core has shut down.
// Explicit plugin unload and swap-chain resize still release them through
// ResetRenderer().
struct RendererStorage {
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    std::vector<FrameContext> frames;
};

RendererStorage* const ProcessRendererStorage = new RendererStorage{};
auto& CommandQueue = ProcessRendererStorage->commandQueue;
auto& CommandList = ProcessRendererStorage->commandList;
auto& RtvHeap = ProcessRendererStorage->rtvHeap;
auto& SrvHeap = ProcessRendererStorage->srvHeap;
auto& Frames = ProcessRendererStorage->frames;
std::chrono::steady_clock::time_point LastFrameTime{};
std::atomic<float> DisplayWidth{1920.0f};
std::atomic<float> DisplayHeight{1080.0f};
std::atomic<ExternalOverlayCallback> ExternalOverlay{};
std::atomic<DiagnosticLogCallback> DiagnosticLogger{};
std::atomic<std::uint64_t> PresentCalls{};
std::atomic<std::uint64_t> DirectQueueCaptures{};
std::atomic<std::uint64_t> RendererInitAttempts{};
std::atomic<std::uint64_t> RendererInitFailures{};
std::atomic<std::uint64_t> RenderedFrames{};
std::atomic<std::uint32_t> LastInitFailureStage{};
std::atomic<std::uint32_t> DiagnosticMessages{};
constexpr std::size_t MaximumNamedOverlays = 8;

enum DiagnosticMessage : std::uint32_t {
    PresentInterceptedMessage = 1u << 0,
    DirectQueueCapturedMessage = 1u << 1,
    RendererInitializedMessage = 1u << 2,
    FirstFrameRenderedMessage = 1u << 3,
    RendererInitFailedMessage = 1u << 4,
};

void LogDiagnosticOnce(
    std::uint32_t messageBit,
    const char* message) noexcept {
    const std::uint32_t previous = DiagnosticMessages.fetch_or(
        messageBit, std::memory_order_acq_rel);
    if ((previous & messageBit) != 0)
        return;
    if (const auto logger = DiagnosticLogger.load(std::memory_order_acquire))
        logger(message);
}

bool FailRendererInitialization(
    std::uint32_t stage,
    const char* message) noexcept {
    RendererInitFailures.fetch_add(1, std::memory_order_relaxed);
    LastInitFailureStage.store(stage, std::memory_order_relaxed);
    LogDiagnosticOnce(RendererInitFailedMessage, message);
    return false;
}

struct NamedOverlayEntry {
    std::array<char, 64> owner{};
    ExternalOverlayCallback callback{};
};

std::mutex NamedOverlayMutex;
std::array<NamedOverlayEntry, MaximumNamedOverlays> NamedOverlays{};

std::vector<std::vector<unsigned char>> EmbeddedFontData;
std::array<ImFont*, kFloatingDamageFontCount> FloatingFonts{};
constexpr std::array<int, kFloatingDamageFontCount> FontResourceIds{
    IDR_FD_FONT_0, IDR_FD_FONT_1, IDR_FD_FONT_2, IDR_FD_FONT_3,
    IDR_FD_FONT_4, IDR_FD_FONT_5, IDR_FD_FONT_6, IDR_FD_FONT_7,
    IDR_FD_FONT_8, IDR_FD_FONT_9, IDR_FD_FONT_10, IDR_FD_FONT_11
};

void ResetRenderer() noexcept {
    std::scoped_lock lock(RenderMutex);
    if (RendererInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    RendererInitialized = false;
    Window = nullptr;
    Frames.clear();
    CommandList.Reset();
    RtvHeap.Reset();
    SrvHeap.Reset();
    CommandQueue.Reset();
    FloatingFonts.fill(nullptr);
    EmbeddedFontData.clear();
    LastFrameTime = {};
}

bool LoadFonts() noexcept {
    EmbeddedFontData.clear();
    EmbeddedFontData.reserve(kFloatingDamageFontCount);
    ImGuiIO& io = ImGui::GetIO();

    for (int index = 0; index < kFloatingDamageFontCount; ++index) {
        const HRSRC resource = FindResourceW(Module, MAKEINTRESOURCEW(FontResourceIds[index]), MAKEINTRESOURCEW(10));
        if (!resource) return false;
        const HGLOBAL loaded = LoadResource(Module, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        const DWORD size = SizeofResource(Module, resource);
        if (!data || size == 0) return false;

        const auto* begin = static_cast<const unsigned char*>(data);
        EmbeddedFontData.emplace_back(begin, begin + size);
        auto& bytes = EmbeddedFontData.back();

        ImFontConfig config{};
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.FontDataOwnedByAtlas = false;
        const std::string label = "FloatingDamageFont" + std::to_string(index);
        strncpy_s(config.Name, label.c_str(), _TRUNCATE);
        const float rasterSize = index == 0 ? 24.0f : 32.0f;
        FloatingFonts[index] = io.Fonts->AddFontFromMemoryTTF(
            bytes.data(), static_cast<int>(bytes.size()), rasterSize, &config, io.Fonts->GetGlyphRangesDefault());
        if (!FloatingFonts[index]) return false;
    }
    return true;
}

bool InitializeRenderer(IDXGISwapChain3* swapChain) noexcept {
    RendererInitAttempts.fetch_add(1, std::memory_order_relaxed);
    ComPtr<ID3D12Device> device;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))))
        return FailRendererInitialization(
            1, "FloatingDamage overlay: renderer initialization failed at swap-chain device lookup.");

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    if (FAILED(swapChain->GetDesc(&swapDesc)) || swapDesc.BufferCount == 0 || !swapDesc.OutputWindow)
        return FailRendererInitialization(
            2, "FloatingDamage overlay: renderer initialization failed at swap-chain description.");
    Window = swapDesc.OutputWindow;
    BackBufferFormat = swapDesc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
        ? DXGI_FORMAT_R8G8B8A8_UNORM
        : swapDesc.BufferDesc.Format;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&SrvHeap))))
        return FailRendererInitialization(
            3, "FloatingDamage overlay: renderer initialization failed at SRV heap creation.");

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = swapDesc.BufferCount;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&RtvHeap))))
        return FailRendererInitialization(
            4, "FloatingDamage overlay: renderer initialization failed at RTV heap creation.");

    Frames.clear();
    Frames.resize(swapDesc.BufferCount);
    auto descriptor = RtvHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT index = 0; index < swapDesc.BufferCount; ++index) {
        auto& frame = Frames[index];
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
            return FailRendererInitialization(
                5, "FloatingDamage overlay: renderer initialization failed at command allocator creation.");
        if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&frame.renderTarget))))
            return FailRendererInitialization(
                6, "FloatingDamage overlay: renderer initialization failed at back-buffer lookup.");
        frame.descriptor = descriptor;
        device->CreateRenderTargetView(frame.renderTarget.Get(), nullptr, descriptor);
        descriptor.ptr += descriptorSize;
    }

    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, Frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&CommandList))))
        return FailRendererInitialization(
            7, "FloatingDamage overlay: renderer initialization failed at command-list creation.");
    if (FAILED(CommandList->Close()))
        return FailRendererInitialization(
            8, "FloatingDamage overlay: renderer initialization failed while closing the command list.");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(Window))
        return FailRendererInitialization(
            9, "FloatingDamage overlay: renderer initialization failed at ImGui Win32 startup.");
    if (!ImGui_ImplDX12_Init(
            device.Get(), swapDesc.BufferCount, BackBufferFormat, SrvHeap.Get(),
            SrvHeap->GetCPUDescriptorHandleForHeapStart(), SrvHeap->GetGPUDescriptorHandleForHeapStart()))
        return FailRendererInitialization(
            10, "FloatingDamage overlay: renderer initialization failed at ImGui DirectX 12 startup.");
    if (!LoadFonts())
        return FailRendererInitialization(
            11, "FloatingDamage overlay: renderer initialization failed while loading embedded fonts.");
    if (!ImGui_ImplDX12_CreateDeviceObjects())
        return FailRendererInitialization(
            12, "FloatingDamage overlay: renderer initialization failed while creating ImGui device objects.");

    LastFrameTime = std::chrono::steady_clock::now();
    RendererInitialized = true;
    LogDiagnosticOnce(
        RendererInitializedMessage,
        "FloatingDamage overlay: ImGui renderer initialized successfully.");
    return true;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags) noexcept {
    PresentCalls.fetch_add(1, std::memory_order_relaxed);
    LogDiagnosticOnce(
        PresentInterceptedMessage,
        "FloatingDamage overlay: intercepted the first game Present call.");
    std::scoped_lock lock(RenderMutex);
    if (!CommandQueue) return OriginalPresent(swapChain, syncInterval, flags);
    if (!RendererInitialized && !InitializeRenderer(swapChain)) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }

    const UINT frameIndex = swapChain->GetCurrentBackBufferIndex();
    if (frameIndex >= Frames.size()) return OriginalPresent(swapChain, syncInterval, flags);
    FrameContext& frame = Frames[frameIndex];

    const auto now = std::chrono::steady_clock::now();
    const float delta = std::clamp(std::chrono::duration<float>(now - LastFrameTime).count(), 0.0f, 0.1f);
    LastFrameTime = now;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    FloatingDamage::PollToggleHotkey(Window);
    const ImGuiIO& io = ImGui::GetIO();
    DisplayWidth.store(io.DisplaySize.x, std::memory_order_relaxed);
    DisplayHeight.store(io.DisplaySize.y, std::memory_order_relaxed);
    FloatingDamage::Update(delta);
    FloatingDamage::Render(ImGui::GetBackgroundDrawList(), io.DisplaySize);
    if (const auto overlay = ExternalOverlay.load(std::memory_order_acquire)) {
        overlay(
            ImGui::GetForegroundDrawList(),
            io.DisplaySize.x,
            io.DisplaySize.y,
            Window);
    }
    std::array<ExternalOverlayCallback, MaximumNamedOverlays> namedCallbacks{};
    {
        std::scoped_lock registryLock(NamedOverlayMutex);
        for (std::size_t index = 0; index < NamedOverlays.size(); ++index) {
            namedCallbacks[index] = NamedOverlays[index].callback;
        }
    }
    for (const auto overlay : namedCallbacks) {
        if (!overlay) continue;
        overlay(
            ImGui::GetForegroundDrawList(),
            io.DisplaySize.x,
            io.DisplaySize.y,
            Window);
    }
    ImGui::Render();

    if (FAILED(frame.allocator->Reset())) return OriginalPresent(swapChain, syncInterval, flags);
    if (FAILED(CommandList->Reset(frame.allocator.Get(), nullptr))) return OriginalPresent(swapChain, syncInterval, flags);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.renderTarget.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    CommandList->ResourceBarrier(1, &barrier);
    CommandList->OMSetRenderTargets(1, &frame.descriptor, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[]{SrvHeap.Get()};
    CommandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), CommandList.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    CommandList->ResourceBarrier(1, &barrier);
    if (FAILED(CommandList->Close())) return OriginalPresent(swapChain, syncInterval, flags);
    ID3D12CommandList* lists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1, lists);
    const std::uint64_t rendered = RenderedFrames.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (rendered == 1) {
        LogDiagnosticOnce(
            FirstFrameRenderedMessage,
            "FloatingDamage overlay: submitted the first ImGui frame to the game command queue.");
    }
    return OriginalPresent(swapChain, syncInterval, flags);
}

void STDMETHODCALLTYPE HookExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* lists
) noexcept {
    if (!CommandQueue && queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        CommandQueue = queue;
        DirectQueueCaptures.fetch_add(1, std::memory_order_relaxed);
        LogDiagnosticOnce(
            DirectQueueCapturedMessage,
            "FloatingDamage overlay: captured the game DirectX 12 command queue.");
    }
    OriginalExecuteCommandLists(queue, count, lists);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(
    IDXGISwapChain3* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags
) noexcept {
    ResetRenderer();
    return OriginalResizeBuffers(swapChain, bufferCount, width, height, format, flags);
}

bool BuildMethodTable() noexcept {
    const wchar_t* className = L"TCPFloatingDamageProbe";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = Module;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    HWND probeWindow = CreateWindowExW(
        0, className, L"TCP Floating Damage Probe", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, Module, nullptr);
    if (!probeWindow) return false;

    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    HMODULE d3d12Module = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12Module) d3d12Module = LoadLibraryW(L"d3d12.dll");
    const auto createDevice = d3d12Module
        ? reinterpret_cast<D3D12CreateDeviceFn>(GetProcAddress(d3d12Module, "D3D12CreateDevice"))
        : nullptr;
    if (!createDevice) {
        DestroyWindow(probeWindow);
        UnregisterClassW(className, Module);
        return false;
    }

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<IDXGISwapChain> swapChain;
    bool success = false;

    do {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) break;
        if (FAILED(createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) break;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) break;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) break;
        if (FAILED(device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) break;

        DXGI_SWAP_CHAIN_DESC swapDesc{};
        swapDesc.BufferDesc.Width = 100;
        swapDesc.BufferDesc.Height = 100;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.OutputWindow = probeWindow;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(queue.Get(), &swapDesc, &swapChain))) break;

        std::memcpy(Methods.data(), *reinterpret_cast<void***>(device.Get()), 44 * sizeof(void*));
        std::memcpy(Methods.data() + 44, *reinterpret_cast<void***>(queue.Get()), 19 * sizeof(void*));
        std::memcpy(Methods.data() + 63, *reinterpret_cast<void***>(allocator.Get()), 9 * sizeof(void*));
        std::memcpy(Methods.data() + 72, *reinterpret_cast<void***>(commandList.Get()), 60 * sizeof(void*));
        std::memcpy(Methods.data() + 132, *reinterpret_cast<void***>(swapChain.Get()), 18 * sizeof(void*));
        success = true;
    } while (false);

    DestroyWindow(probeWindow);
    UnregisterClassW(className, Module);
    return success;
}

bool CreateHook(std::size_t methodIndex, void* target, void** original) noexcept {
    void* address = Methods[methodIndex];
    if (!address) return false;
    const MH_STATUS created = MH_CreateHook(address, target, original);
    if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) return false;
    const MH_STATUS enabled = MH_EnableHook(address);
    return enabled == MH_OK || enabled == MH_ERROR_ENABLED;
}
} // namespace

void SetDllModule(HMODULE module) noexcept {
    Module = module;
}

void SetDiagnosticLogCallback(DiagnosticLogCallback callback) noexcept {
    DiagnosticLogger.store(callback, std::memory_order_release);
}

void SetExternalOverlayCallback(ExternalOverlayCallback callback) noexcept {
    ExternalOverlay.store(callback, std::memory_order_release);
}

bool RegisterNamedExternalOverlay(
    const char* owner,
    ExternalOverlayCallback callback) noexcept {
    if (!owner || owner[0] == '\0') return false;

    // Present owns RenderMutex while invoking callbacks. Taking it here makes
    // unregister a synchronization point so a callback DLL may safely unload.
    std::scoped_lock renderLock(RenderMutex);
    std::scoped_lock registryLock(NamedOverlayMutex);

    NamedOverlayEntry* empty{};
    for (auto& entry : NamedOverlays) {
        if (entry.callback && std::strcmp(entry.owner.data(), owner) == 0) {
            if (callback) entry.callback = callback;
            else entry = {};
            return true;
        }
        if (!entry.callback && !empty) empty = &entry;
    }
    if (!callback) return true;
    if (!empty) return false;

    strncpy_s(empty->owner.data(), empty->owner.size(), owner, _TRUNCATE);
    empty->callback = callback;
    return true;
}

void ClearNamedExternalOverlays() noexcept {
    std::scoped_lock renderLock(RenderMutex);
    std::scoped_lock registryLock(NamedOverlayMutex);
    NamedOverlays = {};
}

namespace {
ImU32 OverlayColor(
    float red,
    float green,
    float blue,
    float alpha) noexcept {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::clamp(red, 0.0f, 1.0f),
        std::clamp(green, 0.0f, 1.0f),
        std::clamp(blue, 0.0f, 1.0f),
        std::clamp(alpha, 0.0f, 1.0f)));
}
} // namespace

void OverlayAddRect(
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
    if (!drawList || right <= left || bottom <= top) return;
    static_cast<ImDrawList*>(drawList)->AddRect(
        ImVec2(left, top), ImVec2(right, bottom),
        OverlayColor(red, green, blue, alpha),
        0.0f, 0, std::max(thickness, 1.0f));
}

void OverlayAddRectFilled(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha) noexcept {
    if (!drawList || right <= left || bottom <= top) return;
    static_cast<ImDrawList*>(drawList)->AddRectFilled(
        ImVec2(left, top), ImVec2(right, bottom),
        OverlayColor(red, green, blue, alpha));
}

void OverlayAddTooltip(
    void* drawList,
    float x,
    float y,
    float displayWidth,
    float displayHeight,
    const char* text) noexcept {
    if (!drawList || !text || text[0] == '\0') return;
    constexpr float Padding = 7.0f;
    constexpr float CursorOffset = 18.0f;
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float width = size.x + Padding * 2.0f;
    const float height = size.y + Padding * 2.0f;
    const float left = std::clamp(
        x + CursorOffset, 0.0f, std::max(0.0f, displayWidth - width));
    const float top = std::clamp(
        y + CursorOffset, 0.0f, std::max(0.0f, displayHeight - height));
    auto* list = static_cast<ImDrawList*>(drawList);
    list->AddRectFilled(
        ImVec2(left, top), ImVec2(left + width, top + height),
        OverlayColor(0.08f, 0.02f, 0.02f, 0.94f), 4.0f);
    list->AddRect(
        ImVec2(left, top), ImVec2(left + width, top + height),
        OverlayColor(1.0f, 0.2f, 0.2f, 0.95f), 4.0f);
    list->AddText(
        ImVec2(left + Padding, top + Padding),
        OverlayColor(1.0f, 0.74f, 0.74f, 1.0f), text);
}

bool InstallHooks() noexcept {
    if (HooksInstalled) return true;
    if (!Module || !BuildMethodTable()) return false;
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (!CreateHook(54, reinterpret_cast<void*>(HookExecuteCommandLists), reinterpret_cast<void**>(&OriginalExecuteCommandLists))) return false;
    if (!CreateHook(140, reinterpret_cast<void*>(HookPresent), reinterpret_cast<void**>(&OriginalPresent))) {
        MH_DisableHook(Methods[54]);
        return false;
    }
    if (!CreateHook(145, reinterpret_cast<void*>(HookResizeBuffers), reinterpret_cast<void**>(&OriginalResizeBuffers))) {
        MH_DisableHook(Methods[140]);
        MH_DisableHook(Methods[54]);
        return false;
    }
    HooksInstalled = true;
    return true;
}

void RemoveHooks() noexcept {
    if (!HooksInstalled) return;
    MH_DisableHook(Methods[145]);
    MH_DisableHook(Methods[140]);
    MH_DisableHook(Methods[54]);
    ResetRenderer();
    HooksInstalled = false;
}

OverlayDiagnostics GetOverlayDiagnostics() noexcept {
    return OverlayDiagnostics{
        .presentCalls = PresentCalls.load(std::memory_order_relaxed),
        .directQueueCaptures = DirectQueueCaptures.load(std::memory_order_relaxed),
        .rendererInitAttempts = RendererInitAttempts.load(std::memory_order_relaxed),
        .rendererInitFailures = RendererInitFailures.load(std::memory_order_relaxed),
        .renderedFrames = RenderedFrames.load(std::memory_order_relaxed),
        .lastInitFailureStage = LastInitFailureStage.load(std::memory_order_relaxed),
        .hooksInstalled = HooksInstalled,
        .commandQueueReady = static_cast<bool>(CommandQueue),
        .rendererInitialized = RendererInitialized,
    };
}

ImFont* GetFloatingDamageFont(int index) noexcept {
    if (index < 0 || index >= kFloatingDamageFontCount) return nullptr;
    return FloatingFonts[static_cast<std::size_t>(index)];
}

void GetDisplaySize(float& width, float& height) noexcept {
    width = DisplayWidth.load(std::memory_order_relaxed);
    height = DisplayHeight.load(std::memory_order_relaxed);
}

} // namespace D3D12
