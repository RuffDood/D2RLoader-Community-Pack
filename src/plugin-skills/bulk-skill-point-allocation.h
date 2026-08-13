#pragma once

#include <D2RLPlugin/api.h>
#include <json.hpp>

namespace RuffnecKk::BulkSkillPointAllocation {

using UiMessageInterceptorFn = bool(__fastcall*)(void*) noexcept;

bool Load(const D2RL::PluginContext* context, const nlohmann::json& skillsConfig) noexcept;
void Unload() noexcept;
bool RegisterUiMessageInterceptor(UiMessageInterceptorFn interceptor) noexcept;
void UnregisterUiMessageInterceptor(UiMessageInterceptorFn interceptor) noexcept;

} // namespace RuffnecKk::BulkSkillPointAllocation
