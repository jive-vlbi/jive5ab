# - Try to find the etransfer
# Usage:
#   find_package(Etransfer)
#
# The code will attempt to search
#* ETRANSFER_ROOT=/path/to/etransfer-github-clone


set(ETRANSFER_SOURCE_DIR)
set(ETRANSFER_SOURCES)
set(ETRANSFER_SUPPORT "no-etransfer")

# Attempt to find xlrapi.h and libssapi.a under the root
if(DEFINED ETRANSFER_ROOT)
    find_path(ETRANSFER_SOURCE_DIR etdc_fd.h
              HINTS ${ETRANSFER_ROOT} PATH_SUFFIXES src)
    if(ETRANSFER_SOURCE_DIR)
        set(ETRANSFER_SOURCES ${ETRANSFER_SOURCE_DIR}/etdc_fd.cc ${ETRANSFER_SOURCE_DIR}/reentrant.cc ${ETRANSFER_SOURCE_DIR}/etdc_debug.cc)
        set(ETRANSFER_SUPPORT ${ETRANSFER_ROOT})
        list(APPEND INSANITY_DEFS  ETRANSFER=1)
        list(APPEND INSANITY_FLAGS -std=c++11)
        message("Compiling in support for e-transfer client [${ETRANSFER_ROOT}]")
    else()
        message(FATAL_ERROR "Did not find a valid e-transfer github checkout at ${ETRANSFER_ROOT}")
    endif(ETRANSFER_SOURCE_DIR)
endif(DEFINED ETRANSFER_ROOT)
