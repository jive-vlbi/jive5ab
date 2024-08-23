// c++11 version of UDT protocol
// 
// A high-speed protocol that could do with improvements in a modern language
// (c) 2024 Marjolein Verkouter
#ifndef LIBUDT11_UDT_H
#define LIBUDT11_UDT_H

#include <udt11_ccc.h>
#include <udt11_api_v1.h>
#include <udt11_options.h>
#include <udt11_exception.h>
#include <udt11_perfmon.h>

// The original UDT library introduces these in the global namespace
using UDTSOCKET = int;
using libudt11::exception::CUDTException;

namespace UDT {
    using libudt11::perfmon::TRACEINFO;
    using libudt11::perfmon::perfmon;
    using libudt11::exception::getlasterror;
    using libudt11::exception::ERROR;
    using ERRORINFO = libudt11::exception::CUDTException;

    // Default to using old-style API
    using namespace libudt11::api::v1;
}


#endif
