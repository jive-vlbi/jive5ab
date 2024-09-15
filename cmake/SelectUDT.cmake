# To be able to experiment w/ different UDT implementations?
#  This is very very crude, but step one is separating concerns
#
# Usage:
#   # In toplevel CMakeLists.txt
#   include(SelectUDT)
#
#   # Cmdline configure option:
#   # -DUDT={udt5ab*, udt11}   (*=default)
#
# import SRT (Secure Reliable Transport) in the root of jive5ab as libsrt5ab
# https://www.srtalliance.org/about-srt-technology/

if(DEFINED UDT)
    set(UDTLIB "${UDT}")
else()
    set(UDTLIB "udt5ab")
endif(DEFINED UDT)

# enforce C++11 if needed
#if("${UDTLIB}" STREQUAL "udt11" OR "${UDTLIB}" STREQUAL "srt5ab")
if("${UDTLIB}" STREQUAL "udt11")
    set(CXXSTANDARD "-std=c++11")
endif()

if("${UDTLIB}" STREQUAL "srt5ab")
    message(FATAL_ERROR "The SRT library is not an UDT implementation")
endif()

# Assumption/requirement:
# all flavours of UDT are found in ./lib<flavour>
set(UDT_SOURCE_DIR  ${CMAKE_SOURCE_DIR}/lib${UDTLIB})

#if("${UDTLIB}" STREQUAL "srt5ab")
#    # has slightly different code layout
#    set(UDT_INCLUDE_DIR ${UDT_SOURCE_DIR}/srtcore)
#    # indicate that we have a different flavour
#    list(APPEND INSANITY_DEFS  UDT_IS_SRT=1)
#else()
#    # libudt5ab/libudt11 share same layout
#    set(UDT_INCLUDE_DIR ${UDT_SOURCE_DIR})
#endif()

# libudt5ab/libudt11 share same layout
set(UDT_INCLUDE_DIR ${UDT_SOURCE_DIR})
