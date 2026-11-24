# Phase 338: the executables' version numbers (versions.cmake at the source root, the one place they are written).
#
#   xeen_version_tuple(LABEL VALUE OUT)  checks VALUE (three or four decimal components, each 0..65535, no leading
#                                        zeros, nothing else) and sets OUT to the Windows resource tuple "a,b,c,d"
#                                        (d = 0 for three components); a FATAL_ERROR naming LABEL otherwise.
#
# Script mode (the phase338_version_syntax test): cmake -DXEEN_VERSION_CHECK=VALUE -P cmake/xeen_version.cmake
# prints "tuple=a,b,c,d" for a valid value and fails with the message for an invalid one.
function(xeen_version_tuple label value out)
    set(component "(0|[1-9][0-9]*)")
    if(NOT value MATCHES "^${component}\\.${component}\\.${component}(\\.${component})?$")
        message(FATAL_ERROR "${label} = \"${value}\" is not a version number: expected three or four decimal "
                            "components (MAJOR.MINOR.PATCH[.BUILD]), each 0..65535 without leading zeros")
    endif()
    set(parts "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
    if("${CMAKE_MATCH_4}" STREQUAL "")
        list(APPEND parts 0)
    else()
        list(APPEND parts "${CMAKE_MATCH_5}")
    endif()
    foreach(part IN LISTS parts)
        string(LENGTH "${part}" digits)
        if(digits GREATER 5 OR part GREATER 65535)
            message(FATAL_ERROR "${label} = \"${value}\": the component ${part} is above 65535 (a Windows version "
                                "resource field is 16 bits)")
        endif()
    endforeach()
    string(REPLACE ";" "," tuple "${parts}")
    set(${out} "${tuple}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND DEFINED XEEN_VERSION_CHECK)
    xeen_version_tuple(XEEN_VERSION_CHECK "${XEEN_VERSION_CHECK}" checked_tuple)
    message("tuple=${checked_tuple}")
endif()
