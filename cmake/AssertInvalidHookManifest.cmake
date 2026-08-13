cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED VALIDATOR OR NOT EXISTS "${VALIDATOR}")
	message(FATAL_ERROR "VALIDATOR must name an existing validator script")
endif()
if(NOT DEFINED INVALID_MANIFEST OR NOT EXISTS "${INVALID_MANIFEST}")
	message(FATAL_ERROR "INVALID_MANIFEST must name an existing test manifest")
endif()

execute_process(
	COMMAND "${CMAKE_COMMAND}" -DHOOK_MANIFEST=${INVALID_MANIFEST} -P ${VALIDATOR}
	RESULT_VARIABLE validation_result
	OUTPUT_VARIABLE validation_output
	ERROR_VARIABLE validation_error
)
string(CONCAT diagnostic "${validation_output}" "${validation_error}")

if(validation_result EQUAL 0)
	message(FATAL_ERROR "Invalid overlap fixture was unexpectedly accepted")
endif()
if(NOT diagnostic MATCHES "Memory-write overlap")
	message(FATAL_ERROR "Invalid fixture failed for the wrong reason:\n${diagnostic}")
endif()

message(STATUS "Overlap rejection VERIFIED")
