file(GLOB PLUGIN_SHARED_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(plugin-shared STATIC ${PLUGIN_SHARED_SOURCES})

target_compile_features(plugin-shared PUBLIC cxx_std_20)
target_compile_definitions(plugin-shared PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(plugin-shared
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PUBLIC ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(plugin-shared PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(BUILD_TESTING)
	add_executable(plugin-shared-json-tests
		tests/plugin-shared-json-tests.cpp
	)
	target_link_libraries(plugin-shared-json-tests PRIVATE plugin-shared)
	add_test(
		NAME plugin-shared-json
		COMMAND plugin-shared-json-tests
	)

	add_executable(plugin-shared-hook-transaction-tests
		tests/plugin-shared-hook-transaction-tests.cpp
	)
	target_link_libraries(plugin-shared-hook-transaction-tests PRIVATE plugin-shared)
	add_test(
		NAME plugin-shared-hook-transaction
		COMMAND plugin-shared-hook-transaction-tests
	)
endif()
