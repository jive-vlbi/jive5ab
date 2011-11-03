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
OPT=-O2
CC=gcc
AS=gcc
CXX=g++
LD=$(CXX) $(LDOPT)

# build jive5ab by default
DEFAULTTARGET=jive5ab

#  Define program(s) you'd like to build from the sources.
#     <prog>_SRC  = sourcefiles to be contained in your program
#     <prog>_LIBS = extra libs your progra, might need
#         only set this variable if you actually need it
jive5ab_SRC=test.cc ioboard.cc dosyscall.cc xlrdevice.cc evlbidebug.cc playpointer.cc transfermode.cc runtime.cc stringutil.cc streamutil.cc hex.cc bin.cc mk5command.cc threadfns.cc getsok.cc registerstuff.cc headersearch.cc userdir.cc busywait.cc byteorder.cc rotzooi.cc timewrap.cc dotzooi.cc dayconversion.cc chain.cc trackmask.cc block.cc circular_buffer.cc variable_type.cc netparms.cc constraints.cc sciprint.cc chainstats.cc version.cc boyer_moore.cc sse_dechannelizer.S blockpool.cc
jive5ab_LIBS=-L$(SSAPILIBD) -L$(WDAPILIBD) $(SSAPILIBS) -lm $(PLATFORMLIBS) -lpthread

tas_SRC=tas.cc sse_dechannelizer.S
tblockpool_SRC=tblockpool.cc blockpool.cc block.cc
tblockpool_LIBS=-lpthread

CCOPT=-fPIC $(OPT) $(M5C) -Wall -W -Werror -Wextra -pedantic -DCC="$(CC)" -DB2B=$(B2B) -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS 
CXXOPT=$(CCOPT) 

# DBG=1 => do debug compile
ifneq ($(strip $(DBG)),)
	OPT = -g -DGDBDEBUG
endif

# MARK5C=1 => compile on Mark5C. Necessary to
#             fix API incompatibilities between Mark5AB and C
ifneq ($(strip $(MARK5C)),)
	M5C=-DMARK5C
endif

# set compiler commands based on bitsize
#  B2B=[32|64] to override whatever the default is (currently: 32)
ifeq ($(B2B),32)
	override AS    += -m32
	override CC    += -m32 -std=c99
	override CXX   += -m32 -ansi
	override LDOPT += -m32 -fPIC
	override CCOPT += -malign-double
	FC=g77 -m32 -g
endif
ifeq ($(B2B),64)
	override AS    += -m64
	override CC    += -m64 -std=c99
	override CXX   += -m64 -ansi
	override LDOPT += -m64 -fPIC
	FC=g77 -m64 -g
endif

# StreamStor stuff. Do autodetection on new or old SDK
SSAPIROOT  = $(shell if [ -d /usr/local/src/streamstor/linux ] ; then echo /usr/local/src/streamstor/linux; fi )
ifeq ($(SSAPIROOT),)
	SSAPIROOT = $(shell if [ -d /home/streamstor/Sdk ] ; then echo /home/streamstor/Sdk; fi)
endif
ifeq ($(SSAPIROOT),)
	SSAPIROOT=/no/streamstor/developmentkit/found
	#error: no streamstor developmentkit found!
endif
SSAPIINCD=$(SSAPIROOT)/include

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
TODO=$(strip $(filter-out Repos/%, $(filter-out chroot, $(filter-out Makefile, $(filter-out clean, $(filter-out info, $(filter-out all, $(MAKECMDGOALS))))))))

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
all: freshversion $(PROG) 
	@echo;echo "Yup. All's up to date matey!"

# pattern rules for fortran/c code

Repos/%.So: %.S
	@if [ ! -d $(REPOS) ]; then\
		mkdir -p $(REPOS);\
	fi;\
	echo;
	$(AS) -c -o $@ $(*F).S ;
	

Repos/%.co: %.c
	@if [ ! -d $(REPOS) ]; then\
		mkdir -p $(REPOS);\
	fi;\
	echo;
	$(CC) $(CCOPT) -c $(INCD) -o $@ $(*F).c ;

Repos/version.cco:
	@echo "Creating version.cco"
	@if [ ! -d Repos ]; then\
	    mkdir -p Repos;\
	fi;\
	printf '#include <string>\nstd::string buildinfo( void ) { return std::string("$(BUILDINFO)");} \n' | $(CXX) $(CXXOPT) -c $(INCD) -o Repos/version.cco -pipe -x c++ -;

Repos/%.cco: %.cc
	@if [ ! -d $(REPOS) ]; then\
	    mkdir -p $(REPOS);\
	fi;\
	echo;
	$(CXX) $(CXXOPT) -c $(INCD) -o $@ $(*F).cc ;

Repos/%.fo: %.f
	@if [ ! -d $(REPOS) ]; then\
		mkdir -p $(REPOS);\
	fi;
	echo;
	$(FC) -c -o $@ $(*F).f ;




# Program specific rules.

ifeq ($(findstring clean, $(MAKECMDGOALS)),)
$(PROG): freshversion $(OBJS) 
	@echo; echo "Building $@" ;
	$(LD) -o $@ $(OBJS) $(LIBD) $(LIBS) $($@_LIBS)
else
$(PROG):
	@echo; echo "Cleaned out $@"
endif

.PHONY: chroot
chroot:
	chown root:root $(PROG); chmod 04555 $(PROG)


clean:
	-rm -f $(OBJS) $(PROG)

info:
	@echo "CC=$(CC)"; echo "CXX=$(CXX)"; echo "OJBS=$(OBJS)"; echo "PROG=$(PROG)"; echo "TODO=$(TODO)"


BUILDINFO=$(shell hostname; date '+%a %d %B %Y %H:%M:%S' )
freshversion:
	@rm -f Repos/version.cco



