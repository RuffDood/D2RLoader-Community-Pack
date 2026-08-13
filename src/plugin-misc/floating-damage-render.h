#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

struct ImFont;

namespace D3D12 {

constexpr int kFloatingDamageFontCount = 12;
using ExternalOverlayCallback = void(__cdecl*)(
    void* drawList, float displayWidth, float displayHeight, HWND window) noexcept;
using DiagnosticLogCallback = void(__cdecl*)(const char* message) noexcept;

struct OverlayDiagnostics {
    std::uint64_t presentCalls{};
    std::uint64_t directQueueCaptures{};
    std::uint64_t rendererInitAttempts{};
    std::uint64_t rendererInitFailures{};
    std::uint64_t renderedFrames{};
    std::uint32_t lastInitFailureStage{};
    bool hooksInstalled{};
    bool commandQueueReady{};
    bool rendererInitialized{};
};

void SetDllModule(HMODULE module) noexcept;
void SetDiagnosticLogCallback(DiagnosticLogCallback callback) noexcept;
void SetExternalOverlayCallback(ExternalOverlayCallback callback) noexcept;
bool RegisterNamedExternalOverlay(
    const char* owner,
    ExternalOverlayCallback callback) noexcept;
void ClearNamedExternalOverlays() noexcept;
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
    float thickness) noexcept;
void OverlayAddRectFilled(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha) noexcept;
void OverlayAddTooltip(
    void* drawList,
    float x,
    float y,
    float displayWidth,
    float displayHeight,
    const char* text) noexcept;
bool InstallHooks() noexcept;
void RemoveHooks() noexcept;
OverlayDiagnostics GetOverlayDiagnostics() noexcept;
ImFont* GetFloatingDamageFont(int index) noexcept;
void GetDisplaySize(float& width, float& height) noexcept;

} // namespace D3D12
