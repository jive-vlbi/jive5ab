# To be able to experiment w/ different UDT implementations?
#  This is very very crude, but step one is separating concerns
#
# Usage:
#   # In toplevel CMakeLists.txt
#   include(SelectUDT)
#
#   # Cmdline configure option:
#   # -DUDT={udt5ab*, udt11, srt5ab}   (*=default)
#
# import SRT (Secure Reliable Transport) in the root of jive5ab as libsrt5ab
# https://www.srtalliance.org/about-srt-technology/

if(DEFINED UDT)
    set(UDTLIB "${UDT}")
else()
    set(UDTLIB "udt5ab")
endif(DEFINED UDT)

# enforce C++11 if needed
if("${UDTLIB}" STREQUAL "udt11" OR "${UDTLIB}" STREQUAL "srt5ab")
    set(CXXSTANDARD "-std=c++11")
endif()

# Assumption/requirement:
# all flavours of UDT are found in ./lib<flavour>
set(UDT_SOURCE_DIR  ${CMAKE_SOURCE_DIR}/lib${UDTLIB})
# libudt5ab/libudt11 share same layout
set(UDT_INCLUDE_DIR ${UDT_SOURCE_DIR})

#if("${UDTLIB}" STREQUAL "srt5ab")
#    # has slightly different code layout
#    set(UDT_INCLUDE_DIR ${UDT_SOURCE_DIR}/srtcore)
#else()
#endif()
