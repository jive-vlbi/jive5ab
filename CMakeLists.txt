project(jive5ab CXX ASM)
cmake_minimum_required (VERSION 2.6.0)

set(CMAKE_MODULE_PATH        ${CMAKE_SOURCE_DIR}/cmake)
include(CheckCXXCompilerFlag)
include(TargetArch)

set(PROJECT_VERSION_MAJOR      3)
set(PROJECT_VERSION_MINOR      0)
set(PROJECT_VERSION_PATCH      0)
set(PROJECT_VERSION_REVISION   0)
set(PROJECT_VERSION            "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
SET(CMAKE_SKIP_RPATH           TRUE)

# get the target architecture
target_architecture(TARGET_ARCH)

# Prepare the BUILDINFO string
execute_process(
    COMMAND bash "-c" "echo \"`hostname` : `date '+%d-%b-%Y : %Hh%Mm%Ss'`\""
    OUTPUT_VARIABLE BUILDINFO
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND bash "-c" "date '+%d-%b-%Y %Hh%Mm%Ss'"
    OUTPUT_VARIABLE  DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
#option(BUILD_SHARED_LIBS "Build shared library(ies)" ON)

# Set policy to allow "ON" to appear in an if() statement
cmake_policy(SET CMP0012 NEW)
# Set policy to require CMakeLists.txt in every source directory
cmake_policy(SET CMP0014 NEW)
# https://cmake.org/cmake/help/latest/policy/CMP0022.html#policy:CMP0022
#cmake_policy(SET CMP0022 OLD)
# https://cmake.org/cmake/help/latest/policy/CMP0074.html
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif(POLICY CMP0074)
# MACOS_RPATH
if(POLICY CMP0042)
    cmake_policy(SET CMP0042 NEW)
endif(POLICY CMP0042)

# RPATH handling - for 3rd party dependencies we want to retain
# the link path in the RPATH thank you very much
#set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# We (that is "I") like to turn up warnings & errors to insanity level
set(INSANITY_FLAGS -pthread -ansi -Wall -W -Werror -Wextra -pedantic -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow)
set(INSANITY_DEFS  _REENTRANT _POSIX_PTHREAD_SEMANTICS __STDC_FORMAT_MACROS _FILE_OFFSET_BITS=64)
# We cannot compile with those options (below) (_POSIX_C_SOURCE && _XOPEN_SOURCE)
# because then we loose IP_ADD_MEMBERSHIP, struct ip_mreq for multicast
# support; POSIX does not define those.
# Also, on MacOSX then mountpoint.cc triggers inclusion of a header which
# claims to have no definitions for u_long (and some other types) such that
# another internal struct has an incomplete definition. 
#set(INSANITY_DEFS  _REENTRANT _POSIX_PTHREAD_SEMANTICS __STDC_FORMAT_MACROS _FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)

# It's nice that people suggest using "modern stuff(tm)" like YOU SHOULD USE add_compile_definitions()
# and shite like target properties. But if you need to use a cmake that doesn't have that you're pretty much
# stuffed are you? FFS.
#add_compile_options(-ansi -Wall -W -Werror -Wextra -pedantic -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow)
#add_compile_definitions(_REENTRANT _POSIX_PTHREAD_SEMANTICS __STDC_FORMAT_MACROS _FILE_OFFSET_BITS=64)


# we want to have options for:
#  FILA specific option
option(FILA10G "Limit recording of UDP packets to support only FiLa10G-Mark5B output" OFF)
if(${FILA10G})
    set(FILA "-FiLa10G")
    list(APPEND INSANITY_DEFS FILA=1)
    #add_compile_definitions(FILA=1)
endif()

#  DEBUG
#    handled through "-DCMAKE_BUILD_TYPE=Debug"

#  SSE=20,41
#     do autodetection
if(NOT DEFINED SSE)
    execute_process(
        COMMAND bash "-c" "if [ -f /proc/cpuinfo ]; then if [ -n \"`grep sse4_1 /proc/cpuinfo`\" ]; then echo \"41\"; else echo \"20\"; fi; else echo \"20\"; fi;"
        OUTPUT_VARIABLE SSE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif(NOT DEFINED SSE)


# For the assembler
# Debian Wheezy has cmake 2.9 (WTF 8-/)
# which does NOT have string concat, append FFS
#string(APPEND CMAKE_ASM_FLAGS " -DSSE=${SSE}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -DSSE=${SSE}")

# These should be handled by cmake/Find{SSAPI,ETRANSFER}.cmake
#  NOSSAPI, SSAPIROOT/WDAPIVER
#  use -DSSAPI_ROOT=/path/to/ssapi [-DWDAPIVER=XXX]
find_package(SSAPI REQUIRED)

# ETRANSFER
# Use -DETRANSFER_ROOT=/path/to/etransfer-github-clone
find_package(Etransfer)

# Debug/Release builds. Set a default build type if none given
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

#string(APPEND CMAKE_C_FLAGS   " -Wbad-function-cast -Wstrict-prototypes")
#string(APPEND CMAKE_CXX_FLAGS " -Wno-long-long -Wno-unused-function")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wbad-function-cast -Wstrict-prototypes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-long-long -Wno-unused-function")

# OpenBSD system headers redeclare the same stuff many times ...
if(${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    #string(APPEND CMAKE_C_FLAGS   " -Wno-redundant-decls")
    #string(APPEND CMAKE_CXX_FLAGS " -Wno-redundant-decls")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -Wno-redundant-decls")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wno-redundant-decls")
endif()

# In order to make really sure that _GNU_SOURCE is *un*defined
# (we don't want no gnu-isms in our code), we must literally
# put this sequence on the compiler command line:
#   -D_GNU_SOURCE -U_GNU_SOURCE 
# Unfortunately, under Linux, doing this breaks everything.
# Defining _POSIX_C_SOURCE/_XOPEN_SOURCE helps but then
# *other* stuff breaks (see above, loss of IP multicast). Sigh.
# But on e.g. MacOSX it's fine to do so so we do - avoiding GNU-isms
# wherever possible.
if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(UNDEF_GNU_SOURCE "-D_GNU_SOURCE -U_GNU_SOURCE")
endif(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")



set(CMAKE_C_FLAGS_RELEASE          "-O2 ${UNDEF_GNU_SOURCE}")
set(CMAKE_CXX_FLAGS_RELEASE        "-O2 ${UNDEF_GNU_SOURCE}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 -g ${UNDEF_GNU_SOURCE}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g ${UNDEF_GNU_SOURCE}")
set(CMAKE_C_FLAGS_DEBUG            "-O0 -g -DGDBDEBUG=1 ${UNDEF_GNU_SOURCE}")
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g -DGDBDEBUG=1 ${UNDEF_GNU_SOURCE}")


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
# NOTE: cmake doesn't fucking honour ASFLAGS (nor CMAKE_ASM_FLAGS) so running
#           CFLAGS=-m32 CXXFLAGS=-m32 (AS|CMAKE_ASM_)FLAGS=-m32 cmake [options] /path/to/here 
#       doesn't fucking work. Thanks guys!
if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
    set(B2B "32")
    #add_compile_definitions(B2B=32)
    list(APPEND INSANITY_DEFS B2B=32)
    #string(APPEND CMAKE_ASM_FLAGS " -m32")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m32")
elseif(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(B2B "64")
    #add_compile_definitions(B2B=64)
    list(APPEND INSANITY_DEFS B2B=64)
    #string(APPEND CMAKE_ASM_FLAGS " -m64")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m64")
else()
    message(FATAL_ERROR "Your machine does not have 32- or 64-bit pointers, it's ${CMAKE_SIZEOF_VOID_P}!")
endif()


add_subdirectory(libudt5ab)
add_subdirectory(src)
add_subdirectory(scripts)

message("Configuration: ${PROJECT_VERSION} ${CMAKE_BUILD_TYPE} WDAPI=${WDAPIVER} SSE=${SSE} FILA=${FILA}")
