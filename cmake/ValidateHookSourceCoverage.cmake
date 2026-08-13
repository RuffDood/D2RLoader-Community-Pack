cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED HOOK_MANIFEST OR NOT EXISTS "${HOOK_MANIFEST}")
	message(FATAL_ERROR "HOOK_MANIFEST must name an existing manifest")
endif()
if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
	message(FATAL_ERROR "SOURCE_ROOT must name the repository source directory")
endif()

file(READ "${HOOK_MANIFEST}" manifest_json)
string(JSON site_count LENGTH "${manifest_json}" sites)
if(site_count EQUAL 0)
	message(FATAL_ERROR "Hook manifest must declare at least one site")
endif()

math(EXPR last_site "${site_count} - 1")
set(declared_ids)
foreach(index RANGE 0 ${last_site})
	string(JSON site_id GET "${manifest_json}" sites ${index} id)
	string(JSON source GET "${manifest_json}" sites ${index} source)
	set(source_path "${SOURCE_ROOT}/../${source}")
	if(NOT EXISTS "${source_path}")
		message(FATAL_ERROR "Manifest site '${site_id}' names missing source '${source}'")
	endif()
	file(READ "${source_path}" source_text)
	set(marker "PSH_MANIFEST_SITE(\"${site_id}\")")
	string(FIND "${source_text}" "${marker}" marker_position)
	if(marker_position EQUAL -1)
		message(FATAL_ERROR
			"Manifest site '${site_id}' has no source marker in '${source}'")
	endif()
	math(EXPR remainder_start "${marker_position} + 1")
	string(SUBSTRING "${source_text}" ${remainder_start} -1 source_remainder)
	string(FIND "${source_remainder}" "${marker}" duplicate_position)
	if(NOT duplicate_position EQUAL -1)
		message(FATAL_ERROR
			"Manifest site '${site_id}' has duplicate source markers in '${source}'")
	endif()
	list(APPEND declared_ids "${site_id}")
endforeach()

file(GLOB_RECURSE plugin_sources
	LIST_DIRECTORIES false
	"${SOURCE_ROOT}/plugin-items/*.cpp"
	"${SOURCE_ROOT}/plugin-levels/*.cpp"
	"${SOURCE_ROOT}/plugin-misc/*.cpp"
	"${SOURCE_ROOT}/plugin-quests/*.cpp"
	"${SOURCE_ROOT}/plugin-skills/*.cpp")

set(marker_count 0)
foreach(source_path IN LISTS plugin_sources)
	file(RELATIVE_PATH source_relative "${SOURCE_ROOT}" "${source_path}")
	if(source_relative MATCHES "^[^/]+/tests/")
		continue()
	endif()
	file(READ "${source_path}" source_text)
	string(REGEX MATCHALL
		"PSH_MANIFEST_SITE\\(\"[A-Za-z0-9_.-]+\"\\)"
		source_markers "${source_text}")
	list(LENGTH source_markers source_marker_count)
	math(EXPR marker_count "${marker_count} + ${source_marker_count}")

	# Ignore comments before looking for direct D2RLoader patch calls. Relay
	# allocation and ordinary memcpy remain legal; executable writes must pass
	# through a PSh_Manifest* wrapper carrying one audited site ID.
	string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" code_without_blocks "${source_text}")
	string(REGEX REPLACE "//[^\n\r]*" "" code_without_comments "${code_without_blocks}")
	if(code_without_comments MATCHES
		"->[ 	]*(PatchBytes|PatchNop|PatchRel32|PatchCallRel32|PatchWrite[A-Za-z0-9_]*|InstallInlineHook)[ 	\r\n]*\\(")
		file(RELATIVE_PATH relative_source "${SOURCE_ROOT}/.." "${source_path}")
		message(FATAL_ERROR
			"Raw executable write found in '${relative_source}'; use a PSh_Manifest* wrapper")
	endif()
	if(code_without_comments MATCHES "PSh_PatchCallSite[ 	\r\n]*\\(")
		file(RELATIVE_PATH relative_source "${SOURCE_ROOT}/.." "${source_path}")
		message(FATAL_ERROR
			"Legacy unregistered call-site write found in '${relative_source}'")
	endif()
endforeach()

if(NOT marker_count EQUAL site_count)
	message(FATAL_ERROR
		"Source marker count ${marker_count} does not match manifest site count ${site_count}")
endif()

message(STATUS
	"Hook source coverage VALID: ${marker_count}/${site_count} executable writes carry unique manifest IDs")
