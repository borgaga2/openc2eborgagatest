# if we don't have .git, just say we don't know
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    message(WARNING "Couldn't parse Git rev, not in a Git repository")
    set(git_short_rev "unknown")
    return()
endif()

# Worktrees and submodules use a .git file pointing at their metadata.
# Let Git resolve it rather than treating that file as a directory.
if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE git_rev_result
            OUTPUT_VARIABLE git_short_rev
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(git_rev_result EQUAL 0)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE git_branch_ref
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            foreach(git_metadata HEAD packed-refs ${git_branch_ref})
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${git_metadata}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE git_metadata_path
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                get_filename_component(git_metadata_path "${git_metadata_path}" ABSOLUTE
                    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${git_metadata_path}")
            endforeach()
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/.git")
            return()
        endif()
    endif()
    message(WARNING "Couldn't parse Git rev from .git file")
    set(git_short_rev "unknown")
    return()
endif()

# set up so CMake re-runs if .git changes
set_property(
	DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/.git"
    "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD"
    "${CMAKE_CURRENT_SOURCE_DIR}/.git/packed-refs"
)

# read .git/HEAD
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD" head_contents)
string(STRIP "${head_contents}" head_contents)

# if it's a bare hash - try to find a matching tag
if (NOT head_contents MATCHES "^ref: ")
    foreach(tag_ref_file IN LISTS tag_ref_files)
        file(READ "${tag_ref_file}" tag_ref_contents)
        string(STRIP "${tag_ref_contents}" tag_ref_contents)
        if(tag_ref_contents STREQUAL head_contents)
            get_filename_component(git_short_rev "${tag_ref_file}" NAME)
            return()
        endif()
    endforeach()
    
    foreach(line IN LISTS packed_ref_contents)
    	if(line STREQUAL "")
    		continue()
    	endif()
        if(line MATCHES "${head_contents} refs/tags/(.*)")
            set(git_short_rev "${CMAKE_MATCH_1}")
            return()
        endif()
    endforeach()
    
    string(SUBSTRING "${head_contents}" 0 8 git_short_rev)
    return()
endif()

# if it's a named ref, find what hash it corresponds to
# (we don't want to return unhelpful branch names like "master")
string(REPLACE "ref: " "" ref_path "${head_contents}")
string(STRIP "${ref_path}" ref_path)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/${ref_path}")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.git/${ref_path}" ref_contents)
	string(SUBSTRING "${ref_contents}" 0 8 git_short_rev)
    return()
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.git/packed-refs" packed_ref_contents)
string(REGEX REPLACE "^#[^\n]*" "" packed_ref_contents "${packed_ref_contents}")
string(REGEX REPLACE ";" "\\\\;" packed_ref_contents "${packed_ref_contents}")
string(REGEX REPLACE "\n" ";" packed_ref_contents "${packed_ref_contents}")
foreach(line IN LISTS packed_ref_contents)
	if(line STREQUAL "")
		continue()
	endif()
	if(line MATCHES "([^ ]+) ${ref_path}")
		string(SUBSTRING "${CMAKE_MATCH_1}" 0 8 git_short_rev)
		return()
	endif()
endforeach()

# all else fails: just say we don't know
message(WARNING "Couldn't parse Git rev")
set(git_short_rev "unknown")