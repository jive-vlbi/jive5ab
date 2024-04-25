#include <udt11_exception.h>

namespace libudt11 { namespace exception {
    const int ERROR = -1;

    CUDTException& getlasterror( void ) {
        static CUDTException  dummy{0, 0, 0};
        return dummy;
    }
} }
