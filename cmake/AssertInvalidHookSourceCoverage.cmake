cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS VALIDATOR INVALID_MANIFEST INVALID_SOURCE_ROOT)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "${required} is required")
	endif()
endforeach()

execute_process(
	COMMAND "${CMAKE_COMMAND}"
		-DHOOK_MANIFEST=${INVALID_MANIFEST}
		-DSOURCE_ROOT=${INVALID_SOURCE_ROOT}
		-P ${VALIDATOR}
	RESULT_VARIABLE validation_result
	OUTPUT_VARIABLE validation_output
	ERROR_VARIABLE validation_error
)
string(CONCAT diagnostic "${validation_output}" "${validation_error}")

if(validation_result EQUAL 0)
	message(FATAL_ERROR "Invalid source-coverage fixture was unexpectedly accepted")
endif()
if(NOT diagnostic MATCHES "Raw executable write")
	message(FATAL_ERROR "Invalid fixture failed for the wrong reason:\n${diagnostic}")
endif()

message(STATUS "Raw executable-write rejection VERIFIED")
