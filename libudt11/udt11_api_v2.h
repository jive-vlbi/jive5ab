// Interface version 2, following (strict?) POSIX types
//
// (c) Marjolein Verkouter
#ifndef LIBUDT11_API_V1_H
#define LIBUDT11_API_V1_H

#include <sys/socket.h>
#include <udt11_options.h>


namespace libudt11 {
    namespace api {
        namespace v2 {
            // Function prototypes from https://pubs.opengroup.org/onlinepubs/9699919799/
            int     socket(int domain, int type, int protocol);
            int     connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
            int     accept(int fd, struct sockaddr* address, socklen_t* address_len);
            int     listen(int fd, int backlog);
#if 0
            // Not implemented in original UDT library
            // possibly not supposed to set standard socket options on UDT sockets at all
            int     getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen);
            int     setsockopt(int fd, int level, int optname, void const* const optval, socklen_t optlen);
#endif
            int     setsockopt(int fd, int level, UDTOpt optname, void const* const optval, socklen_t optlen);
            int     getsockopt(int fd, int level, UDTOpt optname, void* optval, socklen_t* optlen);
            int     close(int fd);
            int     bind(int fd, const struct sockaddr * const address, socklen_t address_len);
            ssize_t recv(int fd, void *buffer, size_t length, int flags);
            ssize_t send(int fd, const void *buffer, size_t length, int flags);
        } // v1
    } // api
} // libudt11

#endif
