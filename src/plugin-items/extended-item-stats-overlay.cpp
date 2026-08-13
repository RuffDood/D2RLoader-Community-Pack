#include "extended-item-stats-overlay.h"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx12.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace ruffneck::extended_item_stats::tooltip_overlay {
namespace {
using Microsoft::WRL::ComPtr;

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> renderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(
    ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

HMODULE Module{};
HMODULE LifetimeModule{};
void* PresentAddress{};
void* ExecuteAddress{};
void* ResizeAddress{};
PresentFn OriginalPresent{};
ExecuteCommandListsFn OriginalExecuteCommandLists{};
ResizeBuffersFn OriginalResizeBuffers{};

SnapshotProvider ProvideSnapshot{};
ScrollRatioHandler HandleScrollRatio{};
ScrollLineHandler HandleScrollLine{};
PinHandler HandlePin{};

std::mutex RenderMutex;
bool HooksInstalled{};
bool RendererInitialized{};
HWND Window{};
DXGI_FORMAT BackBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
HANDLE StopEvent{};
HANDLE Worker{};
std::atomic<bool> Removing{};
std::atomic<std::uint32_t> ActiveCallbacks{};

struct CallbackLease {
    CallbackLease() noexcept {
        ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
    }
    ~CallbackLease() {
        ActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
};

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

std::atomic<bool> Ready{};
std::atomic<LONG> HitLeft{};
std::atomic<LONG> HitTop{};
std::atomic<LONG> HitRight{};
std::atomic<LONG> HitBottom{};
std::atomic<LONG> InteractionLeft{};
std::atomic<LONG> InteractionTop{};
std::atomic<LONG> InteractionRight{};
std::atomic<LONG> InteractionBottom{};
std::atomic<LONG> TrackTopScreen{};
std::atomic<LONG> TrackBottomScreen{};
std::atomic<LONG> ThumbHeightPixels{};
std::atomic<bool> InputDragging{};

void ClearHitRect() noexcept {
    HitLeft = 0;
    HitTop = 0;
    HitRight = 0;
    HitBottom = 0;
    InteractionLeft = 0;
    InteractionTop = 0;
    InteractionRight = 0;
    InteractionBottom = 0;
    TrackTopScreen = 0;
    TrackBottomScreen = 0;
    ThumbHeightPixels = 0;
    InputDragging = false;
}

void ResetRenderer() noexcept {
    std::scoped_lock lock(RenderMutex);
    if (RendererInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui::DestroyContext();
    }
    RendererInitialized = false;
    Ready = false;
    Window = nullptr;
    Frames.clear();
    CommandList.Reset();
    RtvHeap.Reset();
    SrvHeap.Reset();
    CommandQueue.Reset();
    LastFrameTime = {};
    ClearHitRect();
}

bool InitializeRenderer(IDXGISwapChain3* swapChain) noexcept {
    ComPtr<ID3D12Device> device;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device)))) return false;

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    if (FAILED(swapChain->GetDesc(&swapDescription))
        || swapDescription.BufferCount == 0
        || !swapDescription.OutputWindow) {
        return false;
    }
    Window = swapDescription.OutputWindow;
    BackBufferFormat = swapDescription.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
        ? DXGI_FORMAT_R8G8B8A8_UNORM
        : swapDescription.BufferDesc.Format;

    D3D12_DESCRIPTOR_HEAP_DESC shaderDescription{};
    shaderDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shaderDescription.NumDescriptors = 1;
    shaderDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(
            &shaderDescription, IID_PPV_ARGS(&SrvHeap)))) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC targetDescription{};
    targetDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    targetDescription.NumDescriptors = swapDescription.BufferCount;
    if (FAILED(device->CreateDescriptorHeap(
            &targetDescription, IID_PPV_ARGS(&RtvHeap)))) {
        return false;
    }

    Frames.clear();
    Frames.resize(swapDescription.BufferCount);
    auto descriptor = RtvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT index = 0; index < swapDescription.BufferCount; ++index) {
        auto& frame = Frames[index];
        if (FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&frame.allocator)))) {
            return false;
        }
        if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&frame.renderTarget)))) {
            return false;
        }
        frame.descriptor = descriptor;
        device->CreateRenderTargetView(frame.renderTarget.Get(), nullptr, descriptor);
        descriptor.ptr += descriptorSize;
    }

    if (FAILED(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            Frames[0].allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&CommandList)))) {
        return false;
    }
    if (FAILED(CommandList->Close())) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(
        static_cast<float>(swapDescription.BufferDesc.Width),
        static_cast<float>(swapDescription.BufferDesc.Height));
    io.Fonts->AddFontDefault();
    if (!ImGui_ImplDX12_Init(
            device.Get(),
            swapDescription.BufferCount,
            BackBufferFormat,
            SrvHeap.Get(),
            SrvHeap->GetCPUDescriptorHandleForHeapStart(),
            SrvHeap->GetGPUDescriptorHandleForHeapStart())) {
        return false;
    }
    if (!ImGui_ImplDX12_CreateDeviceObjects()) return false;

    LastFrameTime = std::chrono::steady_clock::now();
    RendererInitialized = true;
    Ready = true;
    return true;
}

void DrawTriangle(
    ImDrawList* draw,
    ImVec2 center,
    float radius,
    bool pointsUp,
    ImU32 color) noexcept {
    const auto direction = pointsUp ? -1.0F : 1.0F;
    draw->AddTriangleFilled(
        ImVec2(center.x, center.y + direction * radius),
        ImVec2(center.x - radius, center.y - direction * radius * 0.65F),
        ImVec2(center.x + radius, center.y - direction * radius * 0.65F),
        color);
}

bool ReadColorMarker(
    std::string_view text,
    std::size_t index,
    char& code,
    std::size_t& length) noexcept {
    if (index + 3 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xC3
        && static_cast<unsigned char>(text[index + 1]) == 0xBF
        && text[index + 2] == 'c') {
        code = text[index + 3];
        length = 4;
        return true;
    }
    if (index + 2 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xFF
        && text[index + 1] == 'c') {
        code = text[index + 2];
        length = 3;
        return true;
    }
    if (index + 3 < text.size()
        && static_cast<unsigned char>(text[index]) == 0xEE
        && static_cast<unsigned char>(text[index + 1]) == 0x81
        && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
        code = text[index + 3];
        length = 4;
        return true;
    }
    return false;
}

std::size_t PrivateUseUtf8Length(
    std::string_view text,
    std::size_t index) noexcept {
    if (index + 2 >= text.size()) return 0;

    const auto first = static_cast<unsigned char>(text[index]);
    const auto second = static_cast<unsigned char>(text[index + 1]);
    const auto third = static_cast<unsigned char>(text[index + 2]);
    if ((first & 0xF0) != 0xE0
        || (second & 0xC0) != 0x80
        || (third & 0xC0) != 0x80) {
        return 0;
    }

    const auto codepoint =
        (static_cast<unsigned int>(first & 0x0F) << 12)
        | (static_cast<unsigned int>(second & 0x3F) << 6)
        | static_cast<unsigned int>(third & 0x3F);
    return codepoint >= 0xE000 && codepoint <= 0xF8FF ? 3 : 0;
}

ImU32 TooltipColor(char code) noexcept {
    switch (code) {
    case '1': return IM_COL32(255, 80, 80, 255);
    case '2': return IM_COL32(92, 220, 92, 255);
    case '3': return IM_COL32(104, 104, 255, 255);
    case '4': return IM_COL32(205, 175, 95, 255);
    case '5': return IM_COL32(145, 145, 145, 255);
    case '6': return IM_COL32(45, 45, 45, 255);
    case '7': return IM_COL32(205, 145, 80, 255);
    case '8': return IM_COL32(255, 145, 45, 255);
    case '9': return IM_COL32(245, 225, 90, 255);
    default: return IM_COL32(245, 245, 245, 255);
    }
}

char ColorBefore(std::string_view text, std::size_t end) noexcept {
    char color{'0'};
    end = std::min(end, text.size());
    for (std::size_t index{}; index < end;) {
        char next{};
        std::size_t markerLength{};
        if (ReadColorMarker(text, index, next, markerLength)) {
            color = next;
            index += markerLength;
        } else if (const auto privateLength =
                       PrivateUseUtf8Length(text, index)) {
            index += privateLength;
        } else {
            ++index;
        }
    }
    return color;
}

float MeasureFormattedLine(
    ImFont* font,
    float fontSize,
    std::string_view text,
    std::size_t begin,
    std::size_t end) noexcept {
    if (!font || begin >= end || begin >= text.size()) return 0.0F;
    end = std::min(end, text.size());
    float width{};
    for (auto index = begin; index < end;) {
        char ignored{};
        std::size_t markerLength{};
        if (ReadColorMarker(text, index, ignored, markerLength)) {
            index += markerLength;
            continue;
        }
        if (const auto privateLength = PrivateUseUtf8Length(text, index)) {
            index += privateLength;
            continue;
        }
        auto segmentEnd = index + 1;
        while (segmentEnd < end) {
            if (ReadColorMarker(
                    text, segmentEnd, ignored, markerLength)) {
                break;
            }
            if (PrivateUseUtf8Length(text, segmentEnd)) break;
            ++segmentEnd;
        }
        const auto size = font->CalcTextSizeA(
            fontSize,
            100000.0F,
            0.0F,
            text.data() + index,
            text.data() + segmentEnd);
        width += size.x;
        index = segmentEnd;
    }
    return width;
}

float MaximumFormattedLineWidth(
    ImFont* font,
    float fontSize,
    std::string_view text) noexcept {
    float maximum{};
    for (std::size_t begin{}; begin <= text.size();) {
        const auto separator = text.find('\n', begin);
        const auto end = separator == std::string_view::npos
            ? text.size()
            : separator;
        maximum = std::max(
            maximum,
            MeasureFormattedLine(font, fontSize, text, begin, end));
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    return maximum;
}

void DrawFormattedLine(
    ImDrawList* draw,
    ImFont* font,
    float fontSize,
    std::string_view text,
    std::size_t begin,
    std::size_t end,
    ImVec2 center) noexcept {
    if (!draw || !font || begin > end || end > text.size()) return;
    auto colorCode = ColorBefore(text, begin);
    auto x = center.x
        - MeasureFormattedLine(font, fontSize, text, begin, end) * 0.5F;
    for (auto index = begin; index < end;) {
        char nextColor{};
        std::size_t markerLength{};
        if (ReadColorMarker(text, index, nextColor, markerLength)) {
            colorCode = nextColor;
            index += markerLength;
            continue;
        }
        if (const auto privateLength = PrivateUseUtf8Length(text, index)) {
            index += privateLength;
            continue;
        }
        auto segmentEnd = index + 1;
        while (segmentEnd < end) {
            if (ReadColorMarker(
                    text, segmentEnd, nextColor, markerLength)) {
                break;
            }
            if (PrivateUseUtf8Length(text, segmentEnd)) break;
            ++segmentEnd;
        }
        const auto position = ImVec2(x, center.y - fontSize * 0.5F);
        draw->AddText(
            font,
            fontSize,
            ImVec2(position.x + 1.5F, position.y + 1.5F),
            IM_COL32(0, 0, 0, 235),
            text.data() + index,
            text.data() + segmentEnd);
        draw->AddText(
            font,
            fontSize,
            position,
            TooltipColor(colorCode),
            text.data() + index,
            text.data() + segmentEnd);
        x += font->CalcTextSizeA(
            fontSize,
            100000.0F,
            0.0F,
            text.data() + index,
            text.data() + segmentEnd).x;
        index = segmentEnd;
    }
}

void RenderScrollbar(
    ImDrawList* draw,
    ImVec2 displaySize,
    HWND renderWindow) noexcept {
    Snapshot snapshot{};
    if (!draw || !renderWindow
        || !ProvideSnapshot || !ProvideSnapshot(snapshot)
        || !snapshot.active
        || snapshot.visibleLineCount == 0) {
        InputDragging = false;
        ClearHitRect();
        return;
    }
    const auto showScrollbar = snapshot.scrollable
        && snapshot.totalLineCount > snapshot.visibleLineCount;
    if (!showScrollbar && !snapshot.renderTextInOverlay) {
        InputDragging = false;
        ClearHitRect();
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(renderWindow, &clientRect)) {
        ClearHitRect();
        return;
    }
    const auto clientWidth = static_cast<float>(clientRect.right - clientRect.left);
    const auto clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    if (clientWidth <= 0.0F || clientHeight <= 0.0F
        || displaySize.x <= 0.0F || displaySize.y <= 0.0F) {
        ClearHitRect();
        return;
    }
    const auto clientToRenderX = displaySize.x / clientWidth;
    const auto clientToRenderY = displaySize.y / clientHeight;
    const auto renderToClientX = clientWidth / displaySize.x;
    const auto renderToClientY = clientHeight / displaySize.y;
    const auto anchorX = static_cast<float>(snapshot.anchorClient.x) * clientToRenderX;
    const auto anchorY = static_cast<float>(snapshot.anchorClient.y) * clientToRenderY;

    const auto lineHeight = displaySize.y / 40.0F;
    const auto fontSize = lineHeight * 0.80F;
    auto* font = draw->_Data ? draw->_Data->Font : nullptr;
    const auto barWidth = std::clamp(displaySize.x * 0.008F, 20.0F, 34.0F);
    const auto margin = std::clamp(displaySize.x * 0.004F, 8.0F, 16.0F);
    const auto measuredMaximumWidth = MaximumFormattedLineWidth(
        font, fontSize, snapshot.completeText);
    const auto conservativeMaximumWidth = measuredMaximumWidth > 0.0F
        ? measuredMaximumWidth
            * (snapshot.renderTextInOverlay ? 1.0F : 1.16F)
        : static_cast<float>(snapshot.maximumTextColumns)
            * displaySize.y * 0.012F;
    const auto horizontalPadding = lineHeight * 0.55F;
    const auto reservedScrollbarWidth = showScrollbar
        ? margin + barWidth
        : 0.0F;
    const auto maximumTooltipWidth = std::max(
        displaySize.x * 0.12F,
        displaySize.x - margin * 2.0F - reservedScrollbarWidth);
    const auto tooltipWidth = std::clamp(
        conservativeMaximumWidth + horizontalPadding * 2.0F,
        displaySize.x * 0.12F,
        maximumTooltipWidth);
    const auto maximumTooltipLeft = std::max(
        margin,
        displaySize.x - margin - reservedScrollbarWidth - tooltipWidth);
    const auto tooltipLeft = std::clamp(
        anchorX - tooltipWidth * 0.5F,
        margin,
        maximumTooltipLeft);
    const auto tooltipRight = tooltipLeft + tooltipWidth;
    const auto barLeft = tooltipRight + margin;
    const auto renderedRows = static_cast<float>(snapshot.visibleLineCount);
    const auto barHeight = lineHeight * renderedRows;
    const auto cursorGap = std::clamp(displaySize.y * 0.035F, 32.0F, 72.0F);
    float barTop{};
    if (anchorY - cursorGap >= barHeight + margin) {
        barTop = anchorY - cursorGap - barHeight;
    } else {
        barTop = anchorY + cursorGap;
    }
    barTop = std::clamp(barTop, margin, displaySize.y - barHeight - margin);
    const auto barBottom = barTop + barHeight;
    const auto arrowHeight = barWidth;
    const auto trackTop = barTop + arrowHeight;
    const auto trackBottom = barBottom - arrowHeight;
    const auto trackHeight = std::max(1.0F, trackBottom - trackTop);
    const auto minimumThumbHeight = std::min(barWidth * 1.25F, trackHeight);
    const auto thumbHeight = std::clamp(
        trackHeight * static_cast<float>(snapshot.visibleLineCount)
            / static_cast<float>(std::max<std::size_t>(
                snapshot.totalLineCount, 1)),
        minimumThumbHeight,
        trackHeight);
    const auto maximumFirst = snapshot.totalLineCount > snapshot.visibleLineCount
        ? snapshot.totalLineCount - snapshot.visibleLineCount
        : 0;
    const auto ratio = maximumFirst == 0
        ? 0.0F
        : static_cast<float>(snapshot.firstVisibleLine)
            / static_cast<float>(maximumFirst);
    const auto thumbTop = trackTop + ratio * (trackHeight - thumbHeight);

    POINT origin{clientRect.left, clientRect.top};
    ClientToScreen(renderWindow, &origin);
    if (showScrollbar) {
        HitLeft = origin.x + static_cast<LONG>(
            std::floor((barLeft - margin) * renderToClientX));
        HitTop = origin.y + static_cast<LONG>(
            std::floor((barTop - margin) * renderToClientY));
        HitRight = origin.x + static_cast<LONG>(
            std::ceil((barLeft + barWidth + margin) * renderToClientX));
        HitBottom = origin.y + static_cast<LONG>(
            std::ceil((barBottom + margin) * renderToClientY));
    } else {
        HitLeft = HitTop = HitRight = HitBottom = 0;
        InputDragging = false;
    }
    // Treat the native tooltip body, the item anchor, the corridor leading to
    // the scrollbar and the scrollbar itself as one interaction region. This
    // lets the cursor leave the item and reach the thumb without orphaning the
    // native tooltip on the way.
    InteractionLeft = origin.x + static_cast<LONG>(std::floor(
        (std::min({tooltipLeft, barLeft, anchorX}) - margin) * renderToClientX));
    InteractionTop = origin.y + static_cast<LONG>(std::floor(
        (std::min(barTop, anchorY) - margin) * renderToClientY));
    InteractionRight = origin.x + static_cast<LONG>(std::ceil(
        (std::max({
            tooltipRight,
            showScrollbar ? barLeft + barWidth : tooltipRight,
            anchorX}) + margin)
            * renderToClientX));
    InteractionBottom = origin.y + static_cast<LONG>(std::ceil(
        (std::max(barBottom, anchorY) + margin) * renderToClientY));
    if (showScrollbar) {
        TrackTopScreen = origin.y + static_cast<LONG>(
            std::lround(trackTop * renderToClientY));
        TrackBottomScreen = origin.y + static_cast<LONG>(
            std::lround(trackBottom * renderToClientY));
        ThumbHeightPixels = static_cast<LONG>(
            std::lround(thumbHeight * renderToClientY));
    } else {
        TrackTopScreen = TrackBottomScreen = ThumbHeightPixels = 0;
    }

    if (snapshot.renderTextInOverlay && font && !snapshot.visibleText.empty()) {
        draw->AddRectFilled(
            ImVec2(tooltipLeft, barTop),
            ImVec2(tooltipRight, barBottom),
            IM_COL32(0, 0, 0, 218));
        draw->AddRect(
            ImVec2(tooltipLeft, barTop),
            ImVec2(tooltipRight, barBottom),
            IM_COL32(100, 100, 100, 120),
            0.0F,
            0,
            1.0F);

        auto end = snapshot.visibleText.size();
        for (std::size_t visualLine{};
             visualLine < snapshot.visibleLineCount && end <= snapshot.visibleText.size();
             ++visualLine) {
            const auto separator = end == 0
                ? std::string_view::npos
                : std::string_view(snapshot.visibleText).rfind('\n', end - 1);
            const auto begin = separator == std::string_view::npos
                ? 0
                : separator + 1;
            DrawFormattedLine(
                draw,
                font,
                fontSize,
                snapshot.visibleText,
                begin,
                end,
                ImVec2(
                    (tooltipLeft + tooltipRight) * 0.5F,
                    barTop + lineHeight * (static_cast<float>(visualLine) + 0.5F)));
            if (separator == std::string_view::npos) break;
            end = separator;
        }
    }

    if (!showScrollbar) return;

    const auto border = IM_COL32(25, 18, 8, 240);
    const auto darkGold = IM_COL32(112, 83, 35, 235);
    const auto gold = IM_COL32(174, 132, 56, 245);
    const auto track = IM_COL32(18, 15, 12, 225);
    const auto thumb = gold;

    draw->AddRectFilled(
        ImVec2(barLeft, barTop),
        ImVec2(barLeft + barWidth, barBottom),
        track,
        barWidth * 0.18F);
    draw->AddRect(
        ImVec2(barLeft, barTop),
        ImVec2(barLeft + barWidth, barBottom),
        darkGold,
        barWidth * 0.18F,
        0,
        2.0F);
    DrawTriangle(
        draw,
        ImVec2(barLeft + barWidth * 0.5F, barTop + arrowHeight * 0.5F),
        barWidth * 0.24F,
        true,
        gold);
    DrawTriangle(
        draw,
        ImVec2(barLeft + barWidth * 0.5F, barBottom - arrowHeight * 0.5F),
        barWidth * 0.24F,
        false,
        gold);
    draw->AddRectFilled(
        ImVec2(barLeft + 3.0F, thumbTop),
        ImVec2(barLeft + barWidth - 3.0F, thumbTop + thumbHeight),
        thumb,
        barWidth * 0.16F);
    draw->AddRect(
        ImVec2(barLeft + 3.0F, thumbTop),
        ImVec2(barLeft + barWidth - 3.0F, thumbTop + thumbHeight),
        border,
        barWidth * 0.16F,
        0,
        1.5F);
}

HRESULT STDMETHODCALLTYPE HookPresent(
    IDXGISwapChain3* swapChain,
    UINT syncInterval,
    UINT flags) noexcept {
    CallbackLease callback;
    if (Removing.load(std::memory_order_acquire)) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }
    std::scoped_lock lock(RenderMutex);
    if (!CommandQueue) return OriginalPresent(swapChain, syncInterval, flags);
    if (!RendererInitialized && !InitializeRenderer(swapChain)) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }

    const auto frameIndex = swapChain->GetCurrentBackBufferIndex();
    if (frameIndex >= Frames.size()) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }
    auto& frame = Frames[frameIndex];

    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(swapChain->GetDesc(&description))) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(description.BufferDesc.Width),
        static_cast<float>(description.BufferDesc.Height));
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(
        std::chrono::duration<float>(now - LastFrameTime).count(),
        1.0F / 240.0F,
        0.1F);
    LastFrameTime = now;

    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    RenderScrollbar(ImGui::GetForegroundDrawList(), io.DisplaySize, Window);
    ImGui::Render();

    if (FAILED(frame.allocator->Reset())
        || FAILED(CommandList->Reset(frame.allocator.Get(), nullptr))) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }

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
    if (FAILED(CommandList->Close())) {
        return OriginalPresent(swapChain, syncInterval, flags);
    }
    ID3D12CommandList* lists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1, lists);
    return OriginalPresent(swapChain, syncInterval, flags);
}

void STDMETHODCALLTYPE HookExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* lists) noexcept {
    CallbackLease callback;
    if (Removing.load(std::memory_order_acquire)) {
        OriginalExecuteCommandLists(queue, count, lists);
        return;
    }
    if (!CommandQueue && queue
        && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        CommandQueue = queue;
    }
    OriginalExecuteCommandLists(queue, count, lists);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(
    IDXGISwapChain3* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) noexcept {
    CallbackLease callback;
    if (Removing.load(std::memory_order_acquire)) {
        return OriginalResizeBuffers(
            swapChain, bufferCount, width, height, format, flags);
    }
    ResetRenderer();
    return OriginalResizeBuffers(
        swapChain, bufferCount, width, height, format, flags);
}

bool FindMethodAddresses() noexcept {
    const wchar_t* className = L"ExtendedItemStatsD3D12Probe";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = Module;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const auto probeWindow = CreateWindowExW(
        0,
        className,
        L"Extended Item Stats D3D12 Probe",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        Module,
        nullptr);
    if (!probeWindow) return false;

    using CreateDeviceFn = HRESULT(WINAPI*)(
        IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto d3d12Module = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12Module) d3d12Module = LoadLibraryW(L"d3d12.dll");
    const auto createDevice = d3d12Module
        ? reinterpret_cast<CreateDeviceFn>(
            GetProcAddress(d3d12Module, "D3D12CreateDevice"))
        : nullptr;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain> swapChain;
    bool success{};
    do {
        if (!createDevice) break;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) break;
        if (FAILED(createDevice(
                nullptr,
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device)))) {
            break;
        }
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(
                &queueDescription, IID_PPV_ARGS(&queue)))) {
            break;
        }

        DXGI_SWAP_CHAIN_DESC swapDescription{};
        swapDescription.BufferDesc.Width = 100;
        swapDescription.BufferDesc.Height = 100;
        swapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDescription.SampleDesc.Count = 1;
        swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDescription.BufferCount = 2;
        swapDescription.OutputWindow = probeWindow;
        swapDescription.Windowed = TRUE;
        swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(
                queue.Get(), &swapDescription, &swapChain))) {
            break;
        }

        auto** queueVtable = *reinterpret_cast<void***>(queue.Get());
        auto** swapVtable = *reinterpret_cast<void***>(swapChain.Get());
        ExecuteAddress = queueVtable[10];
        PresentAddress = swapVtable[8];
        ResizeAddress = swapVtable[13];
        success = true;
    } while (false);

    DestroyWindow(probeWindow);
    UnregisterClassW(className, Module);
    return success;
}

bool CreateOwnHook(void* address, void* detour, void** original) noexcept {
    if (!address || !detour || !original) return false;
    const auto created = MH_CreateHook(address, detour, original);
    if (created != MH_OK) return false;
    const auto enabled = MH_EnableHook(address);
    if (enabled == MH_OK) return true;
    MH_RemoveHook(address);
    *original = nullptr;
    return false;
}

bool InstallOwnHooks() noexcept {
    if (HooksInstalled) return true;
    if (!FindMethodAddresses()) return false;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }
    if (!CreateOwnHook(
            ExecuteAddress,
            reinterpret_cast<void*>(HookExecuteCommandLists),
            reinterpret_cast<void**>(&OriginalExecuteCommandLists))) {
        MH_Uninitialize();
        return false;
    }
    if (!CreateOwnHook(
            PresentAddress,
            reinterpret_cast<void*>(HookPresent),
            reinterpret_cast<void**>(&OriginalPresent))) {
        MH_DisableHook(ExecuteAddress);
        MH_RemoveHook(ExecuteAddress);
        OriginalExecuteCommandLists = nullptr;
        MH_Uninitialize();
        return false;
    }
    if (!CreateOwnHook(
            ResizeAddress,
            reinterpret_cast<void*>(HookResizeBuffers),
            reinterpret_cast<void**>(&OriginalResizeBuffers))) {
        MH_DisableHook(PresentAddress);
        MH_DisableHook(ExecuteAddress);
        MH_RemoveHook(PresentAddress);
        MH_RemoveHook(ExecuteAddress);
        OriginalPresent = nullptr;
        OriginalExecuteCommandLists = nullptr;
        MH_Uninitialize();
        return false;
    }
    HooksInstalled = true;
    return true;
}

DWORD WINAPI OverlayWorker(void*) noexcept {
    while (WaitForSingleObject(StopEvent, 250) == WAIT_TIMEOUT) {
        if (InstallOwnHooks()) return 0;
    }
    return 0;
}
} // namespace

void SetCallbacks(
    SnapshotProvider snapshotProvider,
    ScrollRatioHandler scrollRatioHandler,
    ScrollLineHandler scrollLineHandler,
    PinHandler pinHandler) noexcept {
    ProvideSnapshot = snapshotProvider;
    HandleScrollRatio = scrollRatioHandler;
    HandleScrollLine = scrollLineHandler;
    HandlePin = pinHandler;
}

bool Install(HMODULE module) noexcept {
    if (HooksInstalled || Worker) {
        return true;
    }
    if (!module) return false;
    HMODULE retainedModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&Install),
            &retainedModule)) {
        return false;
    }
    Module = retainedModule;
    LifetimeModule = retainedModule;
    Removing.store(false, std::memory_order_release);
    StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!StopEvent) {
        Module = nullptr;
        LifetimeModule = nullptr;
        FreeLibrary(retainedModule);
        return false;
    }
    Worker = CreateThread(nullptr, 0, OverlayWorker, nullptr, 0, nullptr);
    if (Worker) return true;
    CloseHandle(StopEvent);
    StopEvent = nullptr;
    Module = nullptr;
    LifetimeModule = nullptr;
    FreeLibrary(retainedModule);
    return false;
}

bool Remove() noexcept {
    Removing.store(true, std::memory_order_release);
    if (StopEvent) SetEvent(StopEvent);
    if (Worker) {
        const auto wait = WaitForSingleObject(Worker, 3000);
        if (wait != WAIT_OBJECT_0) {
            return false;
        }
        CloseHandle(Worker);
        Worker = nullptr;
    }
    if (StopEvent) {
        CloseHandle(StopEvent);
        StopEvent = nullptr;
    }
    if (HooksInstalled) {
        MH_DisableHook(ResizeAddress);
        MH_DisableHook(PresentAddress);
        MH_DisableHook(ExecuteAddress);
    }
    for (unsigned attempt = 0;
         attempt < 300 && ActiveCallbacks.load(std::memory_order_acquire) != 0;
         ++attempt) {
        Sleep(10);
    }
    if (ActiveCallbacks.load(std::memory_order_acquire) != 0) {
        return false;
    }
    if (HooksInstalled) {
        const auto resizeRemoved = MH_RemoveHook(ResizeAddress) == MH_OK;
        const auto presentRemoved = MH_RemoveHook(PresentAddress) == MH_OK;
        const auto executeRemoved = MH_RemoveHook(ExecuteAddress) == MH_OK;
        const auto removed = resizeRemoved && presentRemoved && executeRemoved;
        const auto uninitialized = MH_Uninitialize() == MH_OK;
        if (!removed || !uninitialized) {
            return false;
        }
        ResizeAddress = nullptr;
        PresentAddress = nullptr;
        ExecuteAddress = nullptr;
        OriginalResizeBuffers = nullptr;
        OriginalPresent = nullptr;
        OriginalExecuteCommandLists = nullptr;
    }
    ResetRenderer();
    HooksInstalled = false;
    const auto retainedModule = LifetimeModule;
    LifetimeModule = nullptr;
    Module = nullptr;
    Removing.store(false, std::memory_order_release);
    if (retainedModule) FreeLibrary(retainedModule);
    return true;
}

bool IsReady() noexcept {
    return Ready.load();
}

bool HitTestScreenPoint(POINT point) noexcept {
    const auto left = HitLeft.load();
    const auto top = HitTop.load();
    const auto right = HitRight.load();
    const auto bottom = HitBottom.load();
    return right > left && bottom > top
        && point.x >= left && point.x <= right
        && point.y >= top && point.y <= bottom;
}

bool HasInteractionRegion() noexcept {
    return InteractionRight.load() > InteractionLeft.load()
        && InteractionBottom.load() > InteractionTop.load();
}

bool InteractionHitTestScreenPoint(POINT point) noexcept {
    const auto left = InteractionLeft.load();
    const auto top = InteractionTop.load();
    const auto right = InteractionRight.load();
    const auto bottom = InteractionBottom.load();
    return right > left && bottom > top
        && point.x >= left && point.x <= right
        && point.y >= top && point.y <= bottom;
}

bool IsDragging() noexcept {
    return InputDragging.load(std::memory_order_acquire);
}

void ClearInteractionRegion() noexcept {
    ClearHitRect();
}

bool HandleMouseInput(WPARAM message, const MSLLHOOKSTRUCT& input) noexcept {
    const auto overScrollbar = HitTestScreenPoint(input.pt);
    const auto trackTop = TrackTopScreen.load();
    const auto trackBottom = TrackBottomScreen.load();
    const auto thumbHeight = ThumbHeightPixels.load();
    const auto updateRatio = [&]() noexcept {
        if (!HandleScrollRatio || trackBottom <= trackTop || thumbHeight <= 0) return;
        const auto travel = std::max<LONG>(1, trackBottom - trackTop - thumbHeight);
        const auto target = std::clamp(
            static_cast<float>(input.pt.y - trackTop) - static_cast<float>(thumbHeight) * 0.5F,
            0.0F,
            static_cast<float>(travel));
        HandleScrollRatio(target / static_cast<float>(travel));
    };

    if (message == WM_LBUTTONDOWN && overScrollbar) {
        if (HandlePin) HandlePin(true);
        if (input.pt.y < trackTop) {
            if (HandleScrollLine) HandleScrollLine(-1);
        } else if (input.pt.y > trackBottom) {
            if (HandleScrollLine) HandleScrollLine(1);
        } else {
            InputDragging.store(true, std::memory_order_release);
            updateRatio();
        }
        return true;
    }
    if (message == WM_MOUSEMOVE
        && InputDragging.load(std::memory_order_acquire)) {
        updateRatio();
        return false;
    }
    if (message == WM_LBUTTONUP
        && InputDragging.exchange(false, std::memory_order_acq_rel)) {
        updateRatio();
        return true;
    }
    return overScrollbar && message == WM_LBUTTONDBLCLK;
}

} // namespace ruffneck::extended_item_stats::tooltip_overlay
