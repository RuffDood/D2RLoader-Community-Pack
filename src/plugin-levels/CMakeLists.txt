file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(plugin-levels SHARED ${PLUGIN_SOURCES} plugin.rc)

target_link_libraries(plugin-levels PRIVATE plugin-shared)

set_target_properties(plugin-levels PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
