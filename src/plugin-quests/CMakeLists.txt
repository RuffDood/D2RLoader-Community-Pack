file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(plugin-quests SHARED ${PLUGIN_SOURCES} plugin.rc)

target_link_libraries(plugin-quests PRIVATE plugin-shared)

set_target_properties(plugin-quests PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(BUILD_TESTING)
	add_executable(larzuk-sockets-policy-tests
		tests/larzuk-sockets-policy-tests.cpp
	)
	target_link_libraries(larzuk-sockets-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		larzuk-sockets-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME larzuk-sockets-policy
		COMMAND larzuk-sockets-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)
endif()
