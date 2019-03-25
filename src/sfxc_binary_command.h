#ifndef EVLBI5A_SFXC_BINARY_COMMAND_H
#define EVLBI5A_SFXC_BINARY_COMMAND_H

#include <string.h>
#include <stdint.h>
#include <ezexcept.h>
#include <runtime.h>

// taken from SFXC's mk5read.h
struct mk5read_msg {
       char            vsn[9];
       char            pad[7];
       uint64_t        off;

       mk5read_msg() { ::memset(this, 0, sizeof(mk5read_msg)); }
};

DECLARE_EZEXCEPT(mk5read_exception)


#endif
