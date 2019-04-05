# - Try to find the streamstor library and include dirs
# Usage:
#   find_package(SSAPI)
#
# The code will attempt to search
#* SSAPI_ROOT=/path/to/streamstor/linux
#  Default: autodetect if present in the standard locations on the
#  current system (may evaluate to "nossapi", see below)
#
#  If you wish to compile with a specific library you can use this
#  setting to point to that one.
#
#* SSAPI_ROOT=nossapi
#  If the library is not found in the standard locations on the current
#  system the autodetect will set SSAPIROOT to this value and force
#  jive5ab to be compiled without Streamstor and Haystack I/O board
#  [present in Mark5A, A+, B and B+] support.
#
#  You can also force jive5ab to be compiled without Streamstor and
#  I/O board support, overriding the (succesfull) autodetection
#  by passing this option explicitly on the 'make' commandline.

if(NOT DEFINED SSAPI_ROOT)
    set(SSAPI_ROOT /usr /usr/local/src/streamstor/linux /home/streamstor/Sdk)
endif()

set(SSAPI_INCLUDE_DIR)
set(SSAPI_LIB)
set(SSAPI_WDAPI)

# Attempt to find xlrapi.h and libssapi.a under the root
if(NOT "${SSAPI_ROOT}" STREQUAL "nossapi")
    foreach(SS_ROOT ${SSAPI_ROOT})
        message("Attempt to find SSAPI in ${SS_ROOT} ...")
        unset(SSAPI_INCl)
        unset(SSAPI_LIBl)
        find_path(SSAPI_INCl xlrapi.h
            HINTS ${SS_ROOT} PATH_SUFFIXES include)
        find_library(SSAPI_LIBl ssapi.a 
            HINTS ${SS_ROOT} PATH_SUFFIXES lib lib/gcc_v4 lib/gcc_v3)
        # if we found both then we say itzok!
        if(SSAPI_INCl AND SSAPI_LIBl)
            message("   FOUND IT: ${SSAPI_INCl} and ${SSAPI_LIBl}")
            # Now check WDAPI library
            # ...
            # Done
            set(SSAPI_INCLUDE_DIR ${SSAPI_INCl} PARENT_SCOPE)
            set(SSAPI_LIB         ${SSAPI_LIBl} PARENT_SCOPE)
            set(SSAPI_ROOT        ${SS_ROOT}    PARENT_SCOPE)
            break()
        endif(SSAPI_INCl AND SSAPI_LIBl)
    endforeach(SS_ROOT ${SSAPI_ROOT})
    # if we found both then we say itzok!
    if(NOT (SSAPI_INCl AND SSAPI_LIBl))
        message(FATAL_ERROR "Unable to find StreamStor include/library. Re-run with -DSSAPI_ROOT=nossapi if no StreamStor support required")
    endif(NOT (SSAPI_INCl AND SSAPI_LIBl))
endif(NOT "${SSAPI_ROOT}" STREQUAL "nossapi")


if("${SSAPI_ROOT}" STREQUAL "nossapi")
    set(SSAPI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/nossapi)
    set(SSAPI_LIB )
    # disable I/O board detection
    #add_compile_definitions(MARK5C=1 NOSSAPI)
    list(APPEND INSANITY_DEFS MARK5C=1 NOSSAPI)
endif("${SSAPI_ROOT}" STREQUAL "nossapi")

