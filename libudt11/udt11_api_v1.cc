// Implementation of the v1 API
//
// (c) Marjolein Verkouter
#include <udt11_api_v1.h>

#include <errno.h>
#include <iostream>
#include <limits>

namespace libudt11 {
    namespace api {
        namespace v1 {

            int socket(int /*domain*/, int /*type*/, int /*protocol*/) {
                std::cout << "libudt11::v1::socket(...)" << std::endl;
                //errno = ENOSYS;
                //return -1;
                return std::numeric_limits<int>::max()-1;
            }

            int connect(int /*fd*/, struct sockaddr const* const/*addr*/, socklen_t /*addrlen*/) {
                std::cout << "libudt11::v1::connect(...)" << std::endl;
                //errno = ENOSYS;
                //return -1;
                return 0;
            }

            int accept(int /*fd*/, struct sockaddr* /*address*/, socklen_t* /*address_len*/) {
                std::cout << "libudt11::v1::accept(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int listen(int /*fd*/, int /*backlog*/) {
                std::cout << "libudt11::v1::listen(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

#if 0
            // Not implemented in original UDT lib
            int setsockopt(int /*fd*/, int /*level*/, int /*optname*/, void const* const /*optval*/, int /*optlen*/) {
                std::cout << "libudt11::v1::setsockopt(...)" << std::endl;
                //errno = ENOSYS;
                //return -1;
                return 0;
            }

            int getsockopt(int /*fd*/, int /*level*/, int /*optname*/, void* /*optval*/, int* /*optlen*/) {
                std::cout << "libudt11::v1::getsockopt(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }
#endif

            int setsockopt(int /*fd*/, int /*level*/, UDTOpt /*optname*/, void const* const /*optval*/, int /*optlen*/) {
                std::cout << "libudt11::v1::setsockopt[UDT](...)" << std::endl;
                //errno = ENOSYS;
                //return -1;
                return 0;
            }

            int getsockopt(int /*fd*/, int /*level*/, UDTOpt /*optname*/, void* /*optval*/, int* /*optlen*/) {
                std::cout << "libudt11::v1::getsockopt[UDT](...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

           int close(int /*fd*/) {
                std::cout << "libudt11::v1::close(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            int bind(int /*fd*/, const struct sockaddr * const /*address*/, socklen_t /*address_len*/) {
                std::cout << "libudt11::v1::bind(...)" << std::endl;
                //errno = ENOSYS;
                //return -1;
                return 0;
            }

            ssize_t recv(int /*fd*/, void * /*buffer*/, size_t /*length*/, int /*flags*/) {
                std::cout << "libudt11::v1::recv(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }

            ssize_t send(int /*fd*/, const void * const/*buffer*/, size_t /*length*/, int /*flags*/) {
                std::cout << "libudt11::v1::send(...)" << std::endl;
                errno = ENOSYS;
                return -1;
            }
        } // v1
    } // api
} // libudt11
