# Move shammy_* packages from the parent of the source tree into releases/.
# dpkg-buildpackage writes them to ".." and this tree's dpkg has no --destdir.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED RELEASE_DIR)
    message(FATAL_ERROR "SOURCE_DIR and RELEASE_DIR are required")
endif()

file(READ "${SOURCE_DIR}/debian/changelog" _clog)
string(REGEX MATCH "\\(([0-9][^)]*)\\)" _m "${_clog}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "could not read version from debian/changelog")
endif()
set(_ver "${CMAKE_MATCH_1}")
string(REGEX MATCH "^[0-9.]+" _upstream "${_ver}")

file(GLOB _artifacts
    "${SOURCE_DIR}/../shammy_${_ver}*"
    "${SOURCE_DIR}/../shammy_${_upstream}*"
    "${SOURCE_DIR}/../shammy_${_upstream}.orig*")
list(REMOVE_DUPLICATES _artifacts)

set(_moved 0)
foreach(_f IN LISTS _artifacts)
    if(IS_DIRECTORY "${_f}")
        continue()
    endif()
    get_filename_component(_name "${_f}" NAME)
    file(COPY "${_f}" DESTINATION "${RELEASE_DIR}")
    file(REMOVE "${_f}")
    message(STATUS "release: ${_name}")
    math(EXPR _moved "${_moved} + 1")
endforeach()

if(_moved EQUAL 0)
    message(FATAL_ERROR "dpkg-buildpackage produced no shammy_${_ver} files in ${SOURCE_DIR}/..")
endif()
