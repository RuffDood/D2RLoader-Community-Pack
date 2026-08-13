file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(plugin-misc SHARED ${PLUGIN_SOURCES} plugin.rc)

target_link_libraries(plugin-misc PRIVATE
	plugin-shared
	pluginpack-imgui
	minhook
	dxgi
)

set_target_properties(plugin-misc PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(BUILD_TESTING)
	add_executable(cube-quick-move-policy-tests
		tests/cube-quick-move-policy-tests.cpp
	)
	target_link_libraries(cube-quick-move-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		cube-quick-move-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME cube-quick-move-policy
		COMMAND cube-quick-move-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)

	add_executable(equipped-item-to-cube-policy-tests
		tests/equipped-item-to-cube-policy-tests.cpp
	)
	target_link_libraries(equipped-item-to-cube-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		equipped-item-to-cube-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME equipped-item-to-cube-policy
		COMMAND equipped-item-to-cube-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)

	add_executable(transmute-hotkey-policy-tests
		tests/transmute-hotkey-policy-tests.cpp
	)
	target_link_libraries(transmute-hotkey-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		transmute-hotkey-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME transmute-hotkey-policy
		COMMAND transmute-hotkey-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)

	add_executable(prevent-merc-death-in-town-policy-tests
		tests/prevent-merc-death-in-town-policy-tests.cpp
	)
	target_link_libraries(
		prevent-merc-death-in-town-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		prevent-merc-death-in-town-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME prevent-merc-death-in-town-policy
		COMMAND prevent-merc-death-in-town-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)

	add_executable(remote-stash-policy-tests
		tests/remote-stash-policy-tests.cpp
	)
	target_link_libraries(remote-stash-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		remote-stash-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME remote-stash-policy
		COMMAND remote-stash-policy-tests
	)
endif()
