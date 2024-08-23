// Implementation of the v1 API
//
// (c) Marjolein Verkouter
#include <udt11_api_v1.h>

#include <errno.h>
#include <iostream>

namespace libudt11 {
    namespace api {
        namespace v2 {

            int socket(int /*domain*/, int /*type*/, int /*protocol*/) {
                std::cout << "libudt11::v2::socket(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int connect(int /*fd*/, struct sockaddr const* const/*addr*/, socklen_t /*addrlen*/) {
                std::cout << "libudt11::v2::connect(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int accept(int /*fd*/, struct sockaddr* /*address*/, socklen_t* /*address_len*/) {
                std::cout << "libudt11::v2::accept(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int listen(int /*fd*/, int /*backlog*/) {
                std::cout << "libudt11::v2::listen(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

#if 0
            // Not implemented in original UDT lib
            int getsockopt(int /*fd*/, int /*level*/, int /*optname*/, void* /*optval*/, socklen_t* /*optlen*/) {
                std::cout << "libudt11::v2::getsockopt(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int setsockopt(int /*fd*/, int /*level*/, int /*optname*/, void const* const /*optval*/, socklen_t /*optlen*/) {
                std::cout << "libudt11::v2::setsockopt(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }
#endif

            int getsockopt(int /*fd*/, int /*level*/, UDTOpt /*optname*/, void* /*optval*/, socklen_t* /*optlen*/) {
                std::cout << "libudt11::v2::getsockopt[UDT](...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int setsockopt(int /*fd*/, int /*level*/, UDTOpt /*optname*/, void const* const /*optval*/, socklen_t /*optlen*/) {
                std::cout << "libudt11::v2::setsockopt[UDT](...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int close(int /*fd*/) {
                std::cout << "libudt11::v2::close(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int bind(int /*fd*/, const struct sockaddr * const /*address*/, socklen_t /*address_len*/) {
                std::cout << "libudt11::v2::bind(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            ssize_t recv(int /*fd*/, void * /*buffer*/, size_t /*length*/, int /*flags*/) {
                std::cout << "libudt11::v2::recv(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            ssize_t send(int /*fd*/, const void * const/*buffer*/, size_t /*length*/, int /*flags*/) {
                std::cout << "libudt11::v2::send(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }
        } // v1
    } // api
} // libudt11
