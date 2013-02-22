# Makefile to create (local) versions of the Mark5* s/w suite w/o
# overwriting existing versions. The binaries will be placed in the
# same directory as this makefile is in
#
# HV: 06-May-2010   * Autodetection of compiler major version so correct
#                   libssapi can be linked against
#                   * changed SSAPIVER to WDAPIVER as it is the WinDriver
#                   API version that's changing and not the SSAPI
#
#     05-Oct-2011   * support for building various programs out of 
#                     the sourcefiles in this directory
#                   * make the objects at least dependant on 
#                     their sourcefiles
#                   * cleaned up - threw away a lot of extra blurb
#
# There should be little need for changing this file;
# only if eg:
#   * new streamstor libssapi.a [for eg different gcc version]
#   * path-to-sources changes [CMASTER/FMASTER for C/Fortran source]

# Set default "bitsize" (32 or 64 bit app) and optimization level.
# Can easily be overridden on cmdline
B2B=32
#OPT=-O2  # See "if (DEBUG)" below
CC=gcc
AS=gcc
CXX=g++
LD=$(CXX) $(LDOPT)
ASOPT=   # See if(DEBUG) belo

ifneq ($(strip $(FILA10G)),)
	FILA=-FiLa10G
	FILAOPT= -DFILA=1
endif

# Attempt to figure out which sse version to use
#SSE=$(shell if [ `echo 'pextrd $0, %xmm0, (%eax)' | as --` ]; then echo "41"; else echo "20"; fi)
SSE=$(shell if [ -n "`grep sse4_1 /proc/cpuinfo`" ]; then echo "41"; else echo "20"; fi)

# build jive5ab by default
DEFAULTTARGET=jive5ab
BUILDINFO=$(shell hostname; echo ":"; date '+%d-%b-%Y : %Hh%Mm%Ss' )
DATE=$(shell date '+%d-%b-%Y %Hh%Mm%Ss')

#GCCSIGN=$(shell major=`$(CXX) -v 2>&1 | grep 'gcc version' | awk '{print $$3;}' | awk -F. '{print $$1;}'`; minor=`$(CXX) -v 2>&1 | grep 'gcc version' | awk '{print $$3;}' | awk -F. '{print $$2;}'`; if [ $${major} -ge 4 -a $${minor} -ge 3 ]; then echo "-Wsign-conversion"; else echo ""; fi )

# on some compilers of the gcc.4.3.<small digit> the "-Wconversion" flag
# is broken - it produces warning for perfectly legitimate code.
# All files that could be fixed are fixed, however atomic.h can't be 
BASEOPT=-fPIC $(OPT) $(M5C) -Wall -W -Werror -Wextra -pedantic -DCC="$(CC)" -DB2B=$(B2B) -DWDAPIVER=$(WDAPIVER) -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -D__STDC_FORMAT_MACROS -Wcast-qual -Wcast-align -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow -D_FILE_OFFSET_BITS=64

CCOPT=$(BASEOPT) $(FILAOPT) -Wbad-function-cast -Wstrict-prototypes
CXXOPT=$(BASEOPT) $(FILAOPT) $(GCCSIGN) -Wno-long-long


ifeq ($(B2B),32)
	override AS    += -m32
	override CC    += -m32
	override CXX   += -m32 -ansi
	override LDOPT += -m32 -fPIC
	override CCOPT += -malign-double
	FC=g77 -m32 -g
endif
ifeq ($(B2B),64)
	override AS    += -m64
	override CC    += -m64
	override CXX   += -m64 -ansi
	override LDOPT += -m64 -fPIC
	FC=g77 -m64 -g
endif

# StreamStor stuff. Do autodetection on new or old SDK (or 'built-in'
# in case of not found)
# To force linking without streamstor libs use "SSAPIROOT=nossapi"
ifeq ($(strip $(SSAPIROOT)),)
SSAPIROOT  = $(shell if [ -d /usr/local/src/streamstor/linux ] ; then echo /usr/local/src/streamstor/linux; fi )
endif
ifeq ($(SSAPIROOT),)
	SSAPIROOT = $(shell if [ -d /home/streamstor/Sdk ] ; then echo /home/streamstor/Sdk; fi)
endif
ifeq ($(SSAPIROOT),)
	SSAPIROOT=nossapi
endif

SSAPIINCD=.
ifneq ($(SSAPIROOT),nossapi)
SSAPIINCD=$(SSAPIROOT)/include
endif

#  Define program(s) you'd like to build from the sources.
#     <prog>_SRC  = sourcefiles to be contained in your program
#     <prog>_LIBS = extra libs your progra, might need
#         only set this variable if you actually need it
jive5ab_SRC=test.cc ioboard.cc dosyscall.cc xlrdevice.cc evlbidebug.cc playpointer.cc transfermode.cc runtime.cc stringutil.cc streamutil.cc hex.cc bin.cc mk5command.cc threadfns.cc getsok.cc registerstuff.cc headersearch.cc userdir.cc busywait.cc byteorder.cc rotzooi.cc timewrap.cc dotzooi.cc dayconversion.cc chain.cc trackmask.cc block.cc variable_type.cc netparms.cc constraints.cc sciprint.cc chainstats.cc version.cc boyer_moore.cc  sse_dechannelizer-$(B2B).S blockpool.cc timezooi.cc splitstuff.cc jit.cc dynamic_channel_extractor.cc buffering.cc data_check.cc mutex_locker.cc getsok_udt.cc mk5_exception.cc threadutil.cc
jive5ab_VERSION=2.3.5-alpha

# targets jive5ab depends upon
jive5ab_DEP=libudt5ab

ifeq ($(SSAPIROOT),nossapi)
OPT += -DNOSSAPI
jive5ab_LIBS=-lm $(PLATFORMLIBS) -lpthread
else
jive5ab_LIBS=-L$(SSAPILIBD) -L$(WDAPILIBD) $(SSAPILIBS) -lm $(PLATFORMLIBS) -lpthread
endif

# Link in support for UDT  
jive5ab_LIBS += -L./libudt5ab -ludt5ab

tas_SRC=tas.cc sse_dechannelizer.S
tas_VERSION=0.1

tnormalize_SRC=tnormalize.cc timezooi.cc
theadersearch_SRC=theadersearch.cc headersearch.cc timezooi.cc dosyscall.cc byteorder.cc hex.cc bin.cc stringutil.cc evlbidebug.cc

tgetsok_SRC=tgetsok.cc getsok.cc evlbidebug.cc dosyscall.cc

tblockpool_SRC=tblockpool.cc blockpool.cc block.cc
tblockpool_LIBS=-lpthread
tblockpool_VERSION=0.1

tsplit2a_SRC=tsplit2a.c sse_dechannelizer-$(B2B).S
tasm_SRC=tasm.cc tasm-$(B2B).S
tasm2_SRC=tasm2.c tasm-$(B2B).S
tsplit3_SRC=tsplit3.c sse_dechannelizer-$(B2B).S
tsplit3a_SRC=tsplit3.cc sse_dechannelizer-$(B2B).S dynamic_channel_extractor.cc jit.cc evlbidebug.cc stringutil.cc dosyscall.cc splitstuff.cc headersearch.cc timezooi.cc
tsplit3a_LIBS=-ldl -lpthread
tsplitstuff_SRC=tsplitstuff.cc stringutil.cc dynamic_channel_extractor.cc
tstringutil_SRC=tstringutil.cc stringutil.cc
tcrc_SRC=tcrc.cc headersearch.cc timezooi.cc stringutil.cc dosyscall.cc

# DEBUG=1 => do debug compile
#  Unfortunately there's a macro in the code called "DEBUG(...)"
#  so we can't compile with "-DDEBUG=1". That was a stupid idea of 
#  me to name the macro that simple ...
RELEASE=release
ifneq ($(strip $(DEBUG)),)
	OPT   += -g -DGDBDEBUG=1 -O0
	ASOPT += -g
	RELEASE=debug
else
	OPT += -O2
endif

# MARK5C=1 => compile on Mark5C. Necessary to
#             fix API incompatibilities between Mark5AB and C
ifeq ($(strip $(SSAPIROOT)),nossapi)
	MARK5C=1
endif
ifneq ($(strip $(MARK5C)),)
	M5C=-DMARK5C -Wno-long-long
endif


# IF we are using one of the systemprovided SSAPI lib (and not the stubbed
# one) we must add "-Wno-long-long" to the compilerflags orelse compilation
# will fail; xlrtypes.h uses 'long long' datatypes which are not part of the
# c++ standard. d'oh.
ifneq ($(findstring $(SSAPIROOT),/usr/local/src/streamstor/linux /home/streamstor/Sdk),)
	override CCOPT += -Wno-long-long
endif

# find WDAPILIBD 
# Mark5B?
WDAPILIBD=$(shell file /usr/local/src/streamstor/linux/driver/lib | sed '/No such file/d' | sed 's/:.*//')
ifeq ($(WDAPILIBD),) 
	# not found? then maybe Mark5A
	# /home/streamstor/linux/driver/lib/libwdapi801.so 
	WDAPILIBD=$(shell file /home/streamstor/linux/driver/lib | sed '/No such file/d' | sed 's/:.*//')
endif

# Still not found - give up
ifeq ($(WDAPILIBD),)
	WDAPILIBD=/no/wdapilib/directory/found
	#error: no WinDriver librarydirectory found
endif

# The actual librarypath is dependant on your gcc major version
# HV/BE 09/Dec/2010 ... yeah it was about time to default to StreamStor
#                   SDK 8.something, ie WDAPIVER 921
GCCVER    = $(shell $(CXX) -v 2>&1 | grep 'gcc version' | awk '{print $$3;}' | cut -b 1)
WDAPIVER  = 921
SSAPILIBD = $(SSAPIROOT)/lib/gcc_v$(GCCVER)
SSAPILIBS = -lssapi -lwdapi$(WDAPIVER) -lm

INCD=-I$(SSAPIINCD) -I. 

PLATFORMLIBS=
ifeq ($(shell uname),Linux)
	PLATFORMLIBS=-lnsl -lrt -ldl
endif

ifeq ($(shell uname),SunOS)
	PLATFORMLIBS=-lnsl -lrt -lsocket -lresolv
endif


# Figure out what to do
TODO=$(strip $(filter-out Repos/%, $(filter-out chown, $(filter-out Makefile, $(filter-out clean, $(filter-out info, $(filter-out all, $(MAKECMDGOALS))))))))

ifeq ($(TODO),)
	TODO:=$(DEFAULTTARGET)
	PROG:=$(DEFAULTTARGET)
else
	PROG=$(firstword $(TODO))
endif

OBJS=$(foreach O, $(patsubst %.c, %.co, $(patsubst %.cc, %.cco, $(patsubst %.S, %.So, $($(PROG)_SRC)))), $(addprefix Repos/, $(O)))

# if PROG is notempty but OBJS is then you've asked for a program to be
# built that is not defined
ifneq ($(PROG),)
ifeq ($(OBJS),)
	error: the target has no sourcelist defined
endif
endif

# The rules
.PHONY: freshversion info all 
all: freshversion $($(PROG)_DEP) $(PROG) 
	@echo;echo "Yup. All's up to date matey!"

# pattern rules for fortran/c code

Repos/%.So: %.S
	@echo;
	@if [ ! -d Repos ]; then \
		mkdir -p Repos; \
	fi
	$(AS) $(ASOPT) -DSSE=$(SSE) -c -o $@ $(*F).S ;

Repos/%.co: %.c
	@echo;
	@if [ ! -d Repos ]; then \
		mkdir -p Repos; \
	fi
	$(CC) $(CCOPT) -c $(INCD) -o $@ $(*F).c ;

Repos/version.cco: 
	@echo "Creating version.cco"
	@if [ ! -d Repos ]; then \
		mkdir -p Repos; \
	fi
	@printf '#include <string>\nstd::string buildinfo( void ) { return std::string(" $(PROG) : $($(PROG)_VERSION)$(FILA) : $(B2B)bit : $(RELEASE) : $(BUILDINFO) : $(SSAPIROOT)");}\nstd::string version_constant( std::string constant ) { if ( constant == "PROG") return "$(PROG)"; if ( constant == "PROG_VERSION" ) return "$($(PROG)_VERSION)$(FILA)"; if ( constant == "B2B" ) return "$(B2B)"; if ( constant == "RELEASE" ) return "$(RELEASE)"; if ( constant == "BUILDINFO"  ) return "$(BUILDINFO)"; if ( constant == "SSAPIROOT") return "$(SSAPIROOT)"; if ( constant == "DATE") return "$(DATE)"; return "";}\n' | $(CXX) $(CXXOPT) -c $(INCD) -o Repos/version.cco -pipe -x c++ -;

Repos/%.cco: %.cc
	@echo;
	@if [ ! -d Repos ]; then \
		mkdir -p Repos; \
	fi
	$(CXX) $(CXXOPT) -c $(INCD) -o $@ $(*F).cc ;

Repos/%.fo: %.f
	@echo;
	@if [ ! -d Repos ]; then \
		mkdir -p Repos; \
	fi
	$(FC) -c -o $@ $(*F).f ;

# target libudt5ab
.PHONY: libudt5ab
libudt5ab:
	@+(export CPP="$(CXX)"; make -C libudt5ab -f Makefile B2B="$(B2B)")


# Program specific rules.

ifeq ($(findstring clean, $(MAKECMDGOALS)),)
$(PROG): $($@_LIBS) freshversion $(OBJS) 
	@echo; echo "Building $@" ;
	$(LD) -o $@ $(OBJS) $(LIBD) $(LIBS) $($@_LIBS)
else
$(PROG):
	@echo; echo "Cleaned out $@"
endif

.PHONY: chown
chown:
	chown root:root $(PROG); chmod 04555 $(PROG)


clean:
	-rm -f $(OBJS) $(PROG); make -C libudt5ab -f Makefile B2B="$(B2B)" clean

info:
	@echo "CC=$(CC)"; echo "CXX=$(CXX)"; echo "OJBS=$(OBJS)"; echo "PROG=$(PROG)"; echo "TODO=$(TODO)"; echo "GCCSIGN=$(GCCSIGN)"; echo "SSE=$(SSE)"; echo "FILA=$(FILA) (FILA10G=$(FILA10G))"; echo "MAKECMDGOALS=$(MAKECMDGOALS)"


freshversion:
	@rm -f Repos/version.cco

