project(jive5ab CXX ASM)
cmake_minimum_required (VERSION 2.6.0)

set(CMAKE_MODULE_PATH        ${CMAKE_SOURCE_DIR}/cmake)
include(CheckCXXCompilerFlag)
include(TargetArch)

set(PROJECT_VERSION_MAJOR    3)
set(PROJECT_VERSION_MINOR    0)
set(PROJECT_VERSION_PATCH    0)
set(PROJECT_VERSION_REVISION 0)
set(PROJECT_VERSION          "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# get the target architecture
target_architecture(TARGET_ARCH)

# Prepare the BUILDINFO string
execute_process(
    COMMAND bash "-c" "hostname ; echo \":\"; date '+%d-%b-%Y : %Hh%Mm%Ss'"
    OUTPUT_VARIABLE BUILDINFO
)



# Set policy to require CMakeLists.txt in every source directory
cmake_policy(SET CMP0014 NEW)
# https://cmake.org/cmake/help/latest/policy/CMP0022.html#policy:CMP0022
#cmake_policy(SET CMP0022 OLD)
# https://cmake.org/cmake/help/latest/policy/CMP0074.html
cmake_policy(SET CMP0074 NEW)

# RPATH handling - for 3rd party dependencies we want to retain
# the link path in the RPATH thank you very much
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# We (that is "I") like to turn up warnings & errors to insanity level
#set(INSANITY_FLAGS -ansi -Wall -W -Werror -Wextra -pedantic -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow)
add_compile_options(-ansi -Wall -W -Werror -Wextra -pedantic -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow)
#set(INSANITY_DEFS  _REENTRANT _POSIX_PTHREAD_SEMANTICS __STDC_FORMAT_MACROS _FILE_OFFSET_BITS=64)
add_compile_definitions(_REENTRANT _POSIX_PTHREAD_SEMANTICS __STDC_FORMAT_MACROS _FILE_OFFSET_BITS=64)

# we want to have options for:
#  FILA specific option
option(FILA10G "Limit recording of UDP packets to support only FiLa10G-Mark5B output" OFF)
if(${FILA10G})
    set(FILA "-FiLa10G")
    #list(APPEND INSANITY_DEFS FILA=1)
    add_compile_definitions(FILA=1)
endif()

#  DEBUG
#    handled through "-DCMAKE_BUILD_TYPE=Debug"

#  SSE=20,41
#     do autodetection
execute_process(
    COMMAND bash "-c" "if [ -f /proc/cpuinfo ]; then if [ -n \"`grep sse4_1 /proc/cpuinfo`\" ]; then echo \"41\" else echo \"20\"; fi; else echo \"20\"; fi;"
    OUTPUT_VARIABLE SSE
)
set(CMAKE_ASM_FLAGS "-DSSE=${SSE}")

# These should be handled by cmake/Find{SSAPI,ETRANSFER}.cmake
#  NOSSAPI, SSAPIROOT/WDAPIVER
#  use -DSSAPI_ROOT=/path/to/ssapi [-DWDAPIVER=XXX]
find_package(SSAPI REQUIRED)

#  ETRANSFER

# Debug/Release builds. Set a default build type if none given
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

string(APPEND CMAKE_C_FLAGS   " -Wbad-function-cast -Wstrict-prototypes")
string(APPEND CMAKE_CXX_FLAGS " -Wno-long-long -Wno-unused-function")

# OpenBSD system headers redeclare the same stuff many times ...
if(${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    string(APPEND CMAKE_CXX_FLAGS " -Wno-redundant-decls")
endif()

set(CMAKE_C_FLAGS_RELEASE          "-O2")
set(CMAKE_CXX_FLAGS_RELEASE        "-O2")
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 -g")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
set(CMAKE_C_FLAGS_DEBUG            "-O0 -g -DGDBDEBUG=1")
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g -DGDBDEBUG=1")


#  forcing 32/64 bit build
#    experienced jive5ab builders will use B2B=32 or B2B=64 
#    but we cannot honour that the same way with cmake.
#    So let's error out and inform users that things have changed.
if(DEFINED B2B)
    message(FATAL_ERROR "Passing B2B=[32|64] as option is not supported anymore. Use:
    CFLAGS=-m32 CXXFLAGS=-m32 cmake [options] /path/to/here
    to force a 32-bit build")
endif()
# do this:
#    $>CFLAGS=-m32 CXXFLAGS=-m32 [/path/to/]cmake [options] /path/to/jive5ab
# to enable 32-bit builds
if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
    set(B2B "32bit")
    add_compile_definitions(B2B=32)
elseif(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(B2B "64bit")
    add_compile_definitions(B2B=64)
else()
    message(FATAL_ERROR "Your machine does not have 32- or 64-bit pointers, it's ${CMAKE_SIZEOF_VOID_P}!")
endif()

# target_compile_options()
# target_compile_definitions()
# target_include_directories()

add_subdirectory(libudt5ab)
add_subdirectory(src)
#add_subdirectory(scripts)

message("Configuration: ${CMAKE_BUILD_TYPE} FILA=${FILA} SSE=${SSE}")
