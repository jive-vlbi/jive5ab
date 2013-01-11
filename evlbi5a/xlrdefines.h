#ifndef EVLBI5A_XLRDEFINES_H
#define EVLBI5A_XLRDEFINES_H

// streamstor api
#include <xlrtypes.h>
#include <xlrapi.h>

// Also the type of the datapointer passed to
// XLRRead* API functions (XLRReadFifo, XLRRead, etc)
// has changed from PULONG (old) to PUINT32
// Code in jive5a[b] uses (as of Dec 2011)
// XLRRead(.., READTYPE* , ...)
//
// On Mark5C's there a newer API than on Mark5A/B
// In the new streamstor API there's no room for
// the type UINT (which was the basic interface
// type on Mark5A/B) but seems to have been replaced
// by UINT32.
// Our code has UINT internally so let's do this 
// and hope the compiler will whine if something
// don't fit.
// HV: 7-dec-2010 Jamie McCallum reported that on
//                Mark5B(+) w. SDK9.* building 
//                fails: this is because the
//                following typedefs should be API
//                dependant, not hardware dependant

#if WDAPIVER>999
// new type
typedef UINT32 READTYPE;
typedef UINT32 UINT;
#else
// old type
typedef ULONG READTYPE;
#endif

// The API on SDK9 has different UINTs for 
// the streamstor channels UINT32 (SDK >= 9.2) vs UINT (others)
// See comment above, 7-dec-2012
#if WDAPIVER>999
typedef UINT32 CHANNELTYPE;
#else
typedef UINT   CHANNELTYPE;
#endif


#ifdef NOSSAPI
// put fn's here that are NOT called via the XLRCALL/XLRCALL2 macro's
// (they should vanish).
XLR_RETURN_CODE XLRClose(SSHANDLE);
UINT            XLRDeviceFind( void );
XLR_RETURN_CODE XLRGetDBInfo(SSHANDLE,PS_DBINFO);
XLR_RETURN_CODE XLRGetErrorMessage(char*,XLR_ERROR_CODE);
DWORDLONG       XLRGetFIFOLength(SSHANDLE);
XLR_ERROR_CODE  XLRGetLastError( void );
DWORDLONG       XLRGetLength(SSHANDLE);
DWORDLONG       XLRGetPlayLength(SSHANDLE);
UINT            XLRGetUserDirLength(SSHANDLE);
XLR_RETURN_CODE XLRReadFifo(SSHANDLE,READTYPE*,ULONG,BOOLEAN);
XLR_RETURN_CODE XLRSkip(SSHANDLE,UINT,BOOLEAN);

#endif
#endif
