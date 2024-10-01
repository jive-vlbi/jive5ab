set(LIBSRT_SRC 
    srtcore/api.cpp
    srtcore/buffer_rcv.cpp
    srtcore/buffer_snd.cpp
    srtcore/buffer_tools.cpp
    srtcore/cache.cpp
    srtcore/channel.cpp
    srtcore/common.cpp
    srtcore/congctl.cpp
    srtcore/srt_compat.c
    srtcore/core.cpp
    srtcore/crypto.cpp
    srtcore/epoll.cpp
    srtcore/fec.cpp
    #   srtcore/group.cpp
    #   srtcore/group_backup.cpp
    #   srtcore/group_common.cpp
    srtcore/handshake.cpp
    srtcore/list.cpp
    srtcore/logger_default.cpp
    srtcore/logger_defs.cpp
    srtcore/logging.cpp
    srtcore/md5.cpp
    srtcore/packet.cpp
    srtcore/packetfilter.cpp
    srtcore/queue.cpp
    srtcore/socketconfig.cpp
    srtcore/srt_c_api.cpp
    srtcore/strerror_defs.cpp
    srtcore/sync.cpp
    srtcore/sync_cxx11.cpp
    #    srtcore/sync_posix.cpp
    srtcore/tsbpd_time.cpp
    srtcore/window.cpp)

# Lifted from the SRT version that we copy into our repo
set (SRT_VERSION 1.5.4)
set_version_variables(SRT_VERSION ${SRT_VERSION})
# Outputs in ${CMAKE_CURRENT_BINARY_DIR} - i.e. build-dir/libsrt5ab/ is taking
configure_file("srtcore/version.h.in" "version.h" @ONLY)

# The srt configuration t we require for our stuff:
# no encryption, having logging (logging at all?)
# but DO:
#   bonding - have to b/c not well enough isolated in the code
#   c++11 - 'cuz it's 2024 
set(SRT5AB_DEFS ENABLE_JUMBO_FRAMES=1 ENABLE_MONOTONIC_CLOCK=1 ENABLE_STDCXX_SYNC=1 SRT_VERSION="${SRT_VERSION}")
#set(SRT5AB_DEFS ENABLE_JUMBO_FRAMES=1 ENABLE_LOGGING=1 ENABLE_HEAVY_LOGGING=1 ENABLE_MONOTONIC_CLOCK=1 ENABLE_STDCXX_SYNC=1 SRT_VERSION="${SRT_VERSION}")
#set(SRT5AB_DEFS HAVE_CXX11 HAVE_FULL_CXX11 ENABLE_MONOTONIC_CLOCK=1 ENABLE_BONDING=1 ENABLE_STDCXX_SYNC=1 ATOMIC_USE_SRT_SYNC_MUTEX=1 SRT_VERSION="${SRT_VERSION}")

add_library(srt5ab STATIC ${LIBSRT_SRC})
set_property(TARGET srt5ab PROPERTY POSITION_INDEPENDENT_CODE TRUE)
set_property(TARGET srt5ab PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
    target_include_directories(srt5ab PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/libsrt5ab/srtcore ${CMAKE_SOURCE_DIR}/libsrt5ab/haicrypt)
else()
    include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/libsrt5ab/srtcore ${CMAKE_SOURCE_DIR}/libsrt5ab/haicrypt)
endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)

# Taken from original UDT Makefile
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    list(APPEND INSANITY_DEFS LINUX)
    #target_compile_definitions(udt5ab PRIVATE LINUX)
    # On Loonix need to manually add pthread and dl libraries ...
    if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
        target_link_libraries(srt5ab PUBLIC pthread dl)
    else()
        target_link_libraries(srt5ab pthread dl)
    endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    list(APPEND INSANITY_DEFS OSX)
    #target_compile_definitions(udt5ab PRIVATE OSX)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "(OpenBSD|SunOS|FreeBSD)")
    list(APPEND INSANITY_DEFS BSD)
    #target_compile_definitions(udt5ab PRIVATE BSD)
else()
    message(FATAL_ERROR "Your system ${CMAKE_SYSTEM_NAME} not currently supported")
endif()

if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
    target_compile_definitions(srt5ab PRIVATE ${INSANITY_DEFS} ${SRT5AB_DEFS})
    target_compile_options(srt5ab PRIVATE     ${INSANITY_FLAGS})
else()
    # COMPILE_FLAGS, COMPILE_OPTIONS
    # Un-FUCKING-believable. Really? Like, REALLY????
    set(INSANITY_DEFS_STR ";${INSANITY_DEFS};")
    set(SRT5AB_DEFS_STR ";${SRT5AB_DEFS};")
    string(REPLACE ";" " -D" INSANITY_DEFS_STR  "${INSANITY_DEFS_STR}")
    string(REPLACE ";" " -D" SRT5AB_DEFS_STR  "${SRT5AB_DEFS_STR}")
    string(REPLACE ";" " " INSANITY_FLAGS_STR "${INSANITY_FLAGS}")
    add_definitions(${INSANITY_DEFS_STR})
    set_target_properties(srt5ab PROPERTIES COMPILE_FLAGS ${INSANITY_FLAGS_STR})
endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
