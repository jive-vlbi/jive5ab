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

#set(SSAPI_INCLUDE_DIR)
#set(SSAPI_LIB)
#set(SSAPI_WDAPI)

# Attempt to find xlrapi.h and libssapi.a under the root
# Note: we could've used find_path(... PATHS ${SSAPI_ROOT} ...)
#       and find_libary(... PATHS ${SSAPI_ROOT} ...) but
#       we want both the header file + library to be found under
#       *the same root*. The default SSAPI_ROOT has > 1 paths
#       so theoretically those independent find_*() calls could
#       find the header in one root and the library in another.
if(NOT "${SSAPI_ROOT}" STREQUAL "nossapi")
    foreach(SS_ROOT ${SSAPI_ROOT})
        message("Attempt to find SSAPI in ${SS_ROOT} ...")
        unset(SSAPI_INCl  CACHE)
        unset(SSAPI_LIBl  CACHE)
        unset(SSAPI_WDAPI CACHE)
        find_path(SSAPI_INCl xlrapi.h
            PATHS ${SS_ROOT} PATH_SUFFIXES include NO_DEFAULT_PATH NO_CMAKE_PATH)
        find_library(SSAPI_LIBl ssapi
            PATHS ${SS_ROOT} PATH_SUFFIXES lib lib/gcc_v4 lib/gcc_v3 NO_DEFAULT_PATH NO_CMAKE_PATH)
        # if we found both then we say itzok!
        if(SSAPI_INCl AND SSAPI_LIBl)
            # Now check WDAPI library
            if(DEFINED WDAPIVER)
                #message("   looking for libwdapi${WDAPIVER}.so ... under ${SS_ROOT}")
                # user gave a specific version, look for that one under the current root
                find_library(SSAPI_WDAPI libwdapi${WDAPIVER}.so
                    PATHS ${SS_ROOT} PATH_SUFFIXES driver/lib lib NO_DEFAULT_PATH NO_CMAKE_PATH)
            else()
                # WDAPIVER not set, find the librar(ies)?
                #message("   WDAPIVER not set, see what we can find")
                # file(GLOB_RECURSE SSAPI_WDAPI SS_ROOT libwdapi*.so) doesn't seem to find *anything*. Thanks guys!
                # So: execute a find and let the shell translate whitespace/newlines to ';' and strip the final one.
                #     No cmake will see that string as a list-of-string. Jeez.
                execute_process(
                    COMMAND bash "-c" "(find ${SS_ROOT} -name libwdapi*.so 2>&1) | tr ' \n' \; | sed 's/;$//'"
                    OUTPUT_VARIABLE SSAPI_WDAPI
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                list(LENGTH SSAPI_WDAPI NLIB)
                #message("   Globresults: ${SSAPI_WDAPI} (NLIB=${NLIB})")
                # Check for useful number of libs (==1)
                if(NLIB LESS 1)
                    message(FATAL_ERROR "Found no wdapi*.so libraries under ${SS_ROOT}?!")
                endif(NLIB LESS 1)
                if(NLIB GREATER 1)
                    message(FATAL_ERROR "Found ${NLIB} wdapi*.so libraries: ${SSAPI_WDAPI}"
                            "\nPlease use -DWDAPIVER=XXXX to select one")
                endif(NLIB GREATER 1)
                # Now extract the WDAPI version number
                # Going to use the shell because sed is a lot more reliable than fucking cmake regex shit
                execute_process(
                    COMMAND bash "-c" "echo \"${SSAPI_WDAPI}\" | sed 's/^.*wdapi\\([0-9]*\\)\\.so/\\1/'"
                    OUTPUT_VARIABLE WDAPIVER
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                # Done
            endif(DEFINED WDAPIVER)
            if(NOT SSAPI_WDAPI)
                message(FATAL_ERROR "Failed to find a wdapiXXXX.so library")
            endif(NOT SSAPI_WDAPI)
            # Set appropriate variables
            set(SSAPI_INCLUDE_DIR     ${SSAPI_INCl})
            set(SSAPI_LIB             ${SSAPI_LIBl})
            list(APPEND INSANITY_DEFS WDAPIVER=${WDAPIVER})
            break()
        endif(SSAPI_INCl AND SSAPI_LIBl)
    endforeach(SS_ROOT ${SSAPI_ROOT})
    # if we found both then we say itzok!
    if(NOT (SSAPI_INCLUDE_DIR AND SSAPI_LIB))
        message(FATAL_ERROR "Unable to find StreamStor include/library. Re-run with -DSSAPI_ROOT=nossapi if no StreamStor support required")
    endif(NOT (SSAPI_INCLUDE_DIR AND SSAPI_LIB))
endif(NOT "${SSAPI_ROOT}" STREQUAL "nossapi")


# No streamstor API required, link in stubs
if("${SSAPI_ROOT}" STREQUAL "nossapi")
    set(SSAPI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/nossapi)
    set(SSAPI_LIB)
    set(SSAPI_WDAPI)
    set(WDAPIVER)
    # disable I/O board detection
    #add_compile_definitions(MARK5C=1 NOSSAPI)
    list(APPEND INSANITY_DEFS MARK5C=1 NOSSAPI)
endif("${SSAPI_ROOT}" STREQUAL "nossapi")

