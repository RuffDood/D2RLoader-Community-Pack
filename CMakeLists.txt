cmake_minimum_required(VERSION 3.24)
project(D2RLPlugins LANGUAGES CXX)

include(CTest)

set(HOOK_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/hook-manifest.json")
set(HOOK_MANIFEST_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateHookManifest.cmake")
set(HOOK_SOURCE_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateHookSourceCoverage.cmake")

execute_process(
	COMMAND "${CMAKE_COMMAND}" -DHOOK_MANIFEST=${HOOK_MANIFEST} -P ${HOOK_MANIFEST_VALIDATOR}
	RESULT_VARIABLE hook_manifest_result
	OUTPUT_VARIABLE hook_manifest_output
	ERROR_VARIABLE hook_manifest_error
)
if(NOT hook_manifest_result EQUAL 0)
	message(FATAL_ERROR "Hook manifest validation failed:\n${hook_manifest_output}${hook_manifest_error}")
endif()
string(STRIP "${hook_manifest_output}" hook_manifest_output)
message(STATUS "${hook_manifest_output}")

execute_process(
	COMMAND "${CMAKE_COMMAND}"
		-DHOOK_MANIFEST=${HOOK_MANIFEST}
		-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src
		-P ${HOOK_SOURCE_VALIDATOR}
	RESULT_VARIABLE hook_source_result
	OUTPUT_VARIABLE hook_source_output
	ERROR_VARIABLE hook_source_error
)
if(NOT hook_source_result EQUAL 0)
	message(FATAL_ERROR "Hook source coverage validation failed:\n${hook_source_output}${hook_source_error}")
endif()
string(STRIP "${hook_source_output}" hook_source_output)
message(STATUS "${hook_source_output}")

add_custom_target(validate-hook-manifest ALL
	COMMAND "${CMAKE_COMMAND}" -DHOOK_MANIFEST=${HOOK_MANIFEST} -P ${HOOK_MANIFEST_VALIDATOR}
	DEPENDS ${HOOK_MANIFEST} ${HOOK_MANIFEST_VALIDATOR}
	COMMENT "Validating hook ownership and memory-write overlaps"
	VERBATIM
)

file(GLOB_RECURSE HOOK_SOURCE_FILES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-items/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-levels/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-misc/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-quests/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-skills/*.cpp")
add_custom_target(validate-hook-source-coverage ALL
	COMMAND "${CMAKE_COMMAND}"
		-DHOOK_MANIFEST=${HOOK_MANIFEST}
		-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src
		-P ${HOOK_SOURCE_VALIDATOR}
	DEPENDS ${HOOK_MANIFEST} ${HOOK_SOURCE_VALIDATOR} ${HOOK_SOURCE_FILES}
	COMMENT "Validating executable-write coverage against the hook manifest"
	VERBATIM
)

add_subdirectory(src)

foreach(plugin_target IN ITEMS plugin-items plugin-levels plugin-misc plugin-quests plugin-skills)
	if(TARGET ${plugin_target})
		add_dependencies(${plugin_target} validate-hook-manifest validate-hook-source-coverage)
	endif()
endforeach()

if(BUILD_TESTING)
	add_test(
		NAME hook-manifest-valid
		COMMAND "${CMAKE_COMMAND}" -DHOOK_MANIFEST=${HOOK_MANIFEST} -P ${HOOK_MANIFEST_VALIDATOR}
	)
	add_test(
		NAME hook-manifest-overlap-rejected
		COMMAND "${CMAKE_COMMAND}"
			-DVALIDATOR=${HOOK_MANIFEST_VALIDATOR}
			-DINVALID_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/hook-manifest-overlap.json
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertInvalidHookManifest.cmake
	)
	add_test(
		NAME hook-manifest-source-coverage
		COMMAND "${CMAKE_COMMAND}"
			-DHOOK_MANIFEST=${HOOK_MANIFEST}
			-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src
			-P ${HOOK_SOURCE_VALIDATOR}
	)
	add_test(
		NAME hook-manifest-raw-write-rejected
		COMMAND "${CMAKE_COMMAND}"
			-DVALIDATOR=${HOOK_SOURCE_VALIDATOR}
			-DINVALID_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/hook-source-coverage-negative/hook-manifest.json
			-DINVALID_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/hook-source-coverage-negative/src
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertInvalidHookSourceCoverage.cmake
	)
endif()
