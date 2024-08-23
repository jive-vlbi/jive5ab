#include <udt11_ccc.h>

namespace libudt11 { namespace ccc {
    CUDTCC::CUDTCC():
        m_dPktSndPeriod( 1.0 )
    {}

    void CUDTCC::init() {}
    void CUDTCC::onACK(int32_t) {}
    void CUDTCC::onLoss(const int32_t*, int) {}
    void CUDTCC::onTimeout() {}

} }
