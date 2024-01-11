set(LIBUDT_SRC api.cpp
buffer.cpp
cache.cpp
ccc.cpp
channel.cpp
common.cpp
core.cpp
epoll.cpp
list.cpp
md5.cpp
packet.cpp
queue.cpp
window.cpp)



add_library(udt5ab STATIC ${LIBUDT_SRC})
#add_library(udt5ab ${LIBUDT_SRC})
set_property(TARGET udt5ab PROPERTY POSITION_INDEPENDENT_CODE TRUE)
set_property(TARGET udt5ab PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
    target_include_directories(udt5ab PUBLIC ${CMAKE_SOURCE_DIR}/libudt5ab)
else()
    include_directories("${CMAKE_SOURCE_DIR}/libudt5ab")
endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)

# Taken from original UDT Makefile
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    list(APPEND INSANITY_DEFS LINUX)
    #target_compile_definitions(udt5ab PRIVATE LINUX)
    # On Loonix need to manually add pthread and dl libraries ...
    if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
        target_link_libraries(udt5ab PUBLIC pthread dl)
    else()
        target_link_libraries(udt5ab pthread dl)
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

## Which architecture
if(${TARGET_ARCH} STREQUAL "i386")
    list(APPEND INSANITY_DEFS IA32)
elseif(${TARGET_ARCH} STREQUAL "x86_64")
    list(APPEND INSANITY_DEFS AMD64)
elseif(${TARGET_ARCH} STREQUAL "arm64")
    list(APPEND INSANITY_DEFS ARM64)
else()
    message(FATAL_ERROR "Your architecture ${TARGET_ARCH} is not yet supported")
endif()

if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
    target_compile_definitions(udt5ab PRIVATE ${INSANITY_DEFS})
    target_compile_options(udt5ab PRIVATE     ${INSANITY_FLAGS})
else()
    # COMPILE_FLAGS, COMPILE_OPTIONS
    # Un-FUCKING-believable. Really? Like, REALLY????
    set(INSANITY_DEFS_STR ";${INSANITY_DEFS}")
    string(REPLACE ";" " -D" INSANITY_DEFS_STR  "${INSANITY_DEFS_STR}")
    string(REPLACE ";" " " INSANITY_FLAGS_STR "${INSANITY_FLAGS}")
    add_definitions(${INSANITY_DEFS_STR})
    set_target_properties(udt5ab PROPERTIES COMPILE_FLAGS ${INSANITY_FLAGS_STR})
endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)

