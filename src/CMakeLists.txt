option(BUILD_PLUGIN_ITEMS  "Build the Items plugin"  ON)
option(BUILD_PLUGIN_LEVELS "Build the Levels plugin" ON)
option(BUILD_PLUGIN_MISC   "Build the Misc plugin"   ON)
option(BUILD_PLUGIN_QUESTS "Build the Quests plugin" ON)
option(BUILD_PLUGIN_SKILLS "Build the Skills plugin" ON)

# Shared graphics and detour dependencies used by the existing Extended Item
# Stats integration and by Floating Damage. Keeping one pinned dependency set
# prevents the two owner DLLs from compiling competing copies at different
# revisions.
include(FetchContent)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	# v1.91.5
	GIT_TAG f401021d5a5d56fe2304056c391e78f81c8d4b8f
)
FetchContent_Declare(
	minhook
	GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
	# v1.3.4
	GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_MakeAvailable(imgui minhook)

add_library(pluginpack-imgui STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(pluginpack-imgui PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
)
target_compile_features(pluginpack-imgui PUBLIC cxx_std_20)
target_compile_definitions(pluginpack-imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(pluginpack-imgui PUBLIC dxgi)

add_subdirectory(plugin-shared)

if(BUILD_PLUGIN_ITEMS)
	add_subdirectory(plugin-items)
endif()

if(BUILD_PLUGIN_LEVELS)
	add_subdirectory(plugin-levels)
endif()

if(BUILD_PLUGIN_MISC)
	add_subdirectory(plugin-misc)
endif()

if(BUILD_PLUGIN_QUESTS)
	add_subdirectory(plugin-quests)
endif()

if(BUILD_PLUGIN_SKILLS)
	add_subdirectory(plugin-skills)
endif()
