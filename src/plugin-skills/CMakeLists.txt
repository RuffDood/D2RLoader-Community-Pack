file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(plugin-skills SHARED ${PLUGIN_SOURCES} plugin.rc)

target_link_libraries(plugin-skills PRIVATE plugin-shared)

set_target_properties(plugin-skills PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(BUILD_TESTING)
	add_executable(bulk-skill-point-allocation-policy-tests
		tests/bulk-skill-point-allocation-policy-tests.cpp
	)
	target_link_libraries(bulk-skill-point-allocation-policy-tests PRIVATE plugin-shared)
	target_include_directories(
		bulk-skill-point-allocation-policy-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(
		NAME bulk-skill-point-allocation-policy
		COMMAND bulk-skill-point-allocation-policy-tests ${CMAKE_SOURCE_DIR}/D2RPlugins.json
	)
endif()
