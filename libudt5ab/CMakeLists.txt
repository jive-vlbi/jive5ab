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
target_include_directories(udt5ab PRIVATE ${CMAKE_SOURCE_DIR}/libudt5ab)

# Taken from original UDT Makefile
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    list(APPEND INSANITY_DEFS LINUX)
    #target_compile_definitions(udt5ab PRIVATE LINUX)
    # On Loonix need to manually add pthread and dl libraries ...
    target_link_libraries(udt5ab PUBLIC pthread dl)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    list(APPEND INSANITY_DEFS OSX)
    #target_compile_definitions(udt5ab PRIVATE OSX)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "(OpenBSD|SunOS)")
    list(APPEND INSANITY_DEFS BSD)
    #target_compile_definitions(udt5ab PRIVATE BSD)
else()
    message(FATAL_ERROR "Your system ${CMAKE_SYSTEM_NAME} not currently supported")
endif()

## Which architecture
if(${TARGET_ARCH} STREQUAL "i386")
    list(APPEND INSANITY_DEFS IA32)
    #target_compile_definitions(udt5ab PRIVATE IA32)
elseif(${TARGET_ARCH} STREQUAL "x86_64")
    list(APPEND INSANITY_DEFS AMD64)
    #target_compile_definitions(udt5ab PRIVATE AMD64)
else()
    message(FATAL_ERROR "Your architecture ${TARGET_ARCHITECTURE} is not yet supported")
endif()

target_compile_definitions(udt5ab PRIVATE ${INSANITY_DEFS})
target_compile_options(udt5ab PRIVATE     ${INSANITY_FLAGS})
