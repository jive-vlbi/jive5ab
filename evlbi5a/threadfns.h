// available thread-functions
#ifndef JIVE5A_THREADFNS_H
#define JIVE5A_THREADFNS_H

#include <map>
#include <string>

// usually the name of the threadfunctions is enough
// info as to what it (supposedly) does.

void* disk2mem( void* );
void* fifo2mem( void* );
void* net2mem( void* );

void* mem2streamstor( void* );
void* mem2net_udp( void* );
void* mem2net_tcp( void* );

// for UDP helper thread we have a choice of methods.
// the user can set which one he/she wishes to use
// The code should look in this map if the requested
// helper is configured/available
typedef std::map<std::string, void* (*)(void*)> udphelper_maptype;

// Get the map of "name" => threadfunction, which contains
// the currently available udphelpers
const udphelper_maptype& udphelper_map( void );

#endif
