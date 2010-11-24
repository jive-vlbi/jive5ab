# Makefile to create (local) versions of the Mark5* s/w suite w/o
# overwriting existing versions. The binaries will be placed in the
# same directory as this makefile is in
#
# HV: 06-May-2010   * Autodetection of compiler major version so correct
#                   libssapi can be linked against
#                   * changed SSAPIVER to WDAPIVER as it is the WinDriver
#                   API version that's changing and not the SSAPI

# There should be little need for changing this file;
# only if eg:
#   * new streamstor libssapi.a [for eg different gcc version]
#   * path-to-sources changes [CMASTER/FMASTER for C/Fortran source]

# Set default "bitsize" (32 or 64 bit app) and optimization level.
# Can easily be overridden on cmdline
B2B=32
OPT=-O2
ifneq ($(strip $(DBG)),)
	OPT = -g -DGDBDEBUG
endif

# set compiler commands based on bitsize
ifeq ($(B2B),32)
	CC=gcc -m32 -fPIC $(OPT) -Wall -W -Werror -malign-double -DB2B=$(B2B) -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
	CXX=g++ -m32 -fPIC $(OPT) -Wall -W -Werror -malign-double -DB2B=$(B2B) -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
	FC=g77 -m32 -g
endif
ifeq ($(B2B),64)
	CC=gcc -m64 -fPIC $(OPT) -Wall -W -Werror -DB2B=$(B2B) -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
	CXX=g++ -m64 -fPIC $(OPT) -Wall -W -Werror -DB2B=$(B2B) -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
	FC=g77 -m64 -g
endif

# StreamStor stuff. Do autodetection on new or old SDK
SSAPIROOT  = $(shell if [ -d /usr/local/src/streamstor/linux ] ; then echo /usr/local/src/streamstor/linux; fi )
ifeq ($(SSAPIROOT),)
	SSAPIROOT = $(shell if [ -d /home/streamstor/Sdk ] ; then echo /home/streamstor/Sdk; fi)
endif
ifeq ($(SSAPIROOT),)
	error: no streamstor developmentkit found!
endif
SSAPIINCD=$(SSAPIROOT)/include

# find WDAPILIBD 
# Mark5B?
WDAPILIBD=$(shell file /usr/local/src/streamstor/linux/driver/lib | sed '/No such file/d')
ifeq ($(WDAPILIBD),) 
# not found? then maybe Mark5A
# /home/streamstor/linux/driver/lib/libwdapi801.so 
	WDAPILIBD=$(shell file /home/streamstor/linux/driver/lib | sed '/No such file/d')
endif

# Still not found - give up
ifeq ($(WDAPILIBD),)
	error: no WinDriver librarydirectory found
endif

# The actual librarypath is dependant on your gcc major version
GCCVER    = $(shell g++ -v 2>&1 | grep version | awk '{print $$3;}' | cut -b 1)
WDAPIVER  = 801
SSAPILIBD = $(SSAPIROOT)/lib/gcc_v$(GCCVER)
SSAPILIBS = -lssapi -lwdapi$(WDAPIVER) -lm

GLIBINCD=$(shell glib-config --cflags | sed '/command not/d')
ifeq ($(GLIBINCD),)
	GLIBINCD=$(shell pkg-config --cflags glib-2.0 | sed '/command not/d')
endif
INCD=-I$(SSAPIINCD) -I. $(GLIBINCD) 

PLATFORMLIBS=
ifeq ($(shell uname),Linux)
	PLATFORMLIBS=-lnsl -lrt -ldl
endif

# All libraries that you need to linx0r in to have
# a working proggie
# they are app specific 
Mark5A_LIBS=-L$(SSAPILIBD) -L$(WDAPILIBD) -lcurses -lpthread $(SSAPILIBS) -lm
tstMark5A_LIBS=-lreadline -lcurses
jive5ab_LIBS=-L$(SSAPILIBD) -L$(WDAPILIBD) $(SSAPILIBS) -lm $(PLATFORMLIBS) -lpthread
tmem_LIBS=-ldl -lm -lpthread
tchain_LIBS=-ldl -lm -lpthread
tmask_LIBS=-ldl -lm -lpthread
tconstrain_LIBS=-ldl -lm -lpthread

# important directories

# where is the master source-code located?
# default to here, "release" is /home/jball/C
CMASTER=.
FMASTER=/home/jball/F

# where to put the object files
REPOS=Repos/

# List the sources which make up the progs
Mark5A_SRC=Mark5A.c Parse5A.c IOBoard.c Mark5mess.c rjc_code.c transpose.c mess.c dazl.f daze.f
tstMark5A_SRC=tstMark5A.c



# Programs to build
PROGS=jive5ab #Mark5A tstMark5A 

# pattern rules for fortran/c code

%.co:
	@if [ ! -d $(REPOS) ]; then\
		mkdir -p $(REPOS);\
	fi;\
	echo;
	$(CXX) -c $(INCD) -o $@ $(CMASTER)/$(*F).c ;

%.cco:
	@if [ ! -d $(REPOS) ]; then\
	    mkdir -p $(REPOS);\
	fi;\
	echo;
	$(CXX) -c $(INCD) -o $@ $(CMASTER)/$(*F).cc ;

%.fo:
	@if [ ! -d $(REPOS) ]; then\
		mkdir -p $(REPOS);\
	fi;
	echo;
	$(FC) -c -o $@ $(FMASTER)/$(*F).f ;


Mark5A_OBJ=$(patsubst %.c, %.co, $(patsubst %.f, %.fo, $(Mark5A_SRC)))
tstMark5A_OBJ=$(patsubst %.c, %.co, $(patsubst %.f, %.fo, $(tstMark5A_SRC)))


# The rules
all: $(PROGS)
	@echo;echo "Yup. All's up to date matey!"


# Program specific rules.
# Would like to generalize even more but that is not going to work with
# the current dinosaur-version of gmake installed on the Mk5s

#Mark5A: $(foreach O, $(patsubst %.c, %.co, $(patsubst %.f, %.fo, $($@_SRC))), $(addprefix $(REPOS),$(O))) 
Mark5A: $(foreach O, $(patsubst %.c, %.co, $(patsubst %.f, %.fo, $(Mark5A_SRC))), $(addprefix $(REPOS),$(O))) 
	@echo; echo "Linking $@"; echo
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)
	chown root:root $@; chmod 04555 $@

tstMark5A: $(foreach O, $(patsubst %.c, %.co, $(patsubst %.f, %.fo, $(tstMark5A_SRC))), $(addprefix $(REPOS),$(O))) 
	@echo; echo "Linking $@"; echo
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)
	chown jball:users $@

jive5ab: Repos/test.cco Repos/ioboard.cco Repos/dosyscall.cco Repos/xlrdevice.cco Repos/evlbidebug.cco Repos/playpointer.cco Repos/transfermode.cco Repos/runtime.cco Repos/stringutil.cco Repos/streamutil.cco Repos/hex.cco Repos/bin.cco Repos/mk5command.cco Repos/threadfns.cco Repos/getsok.cco Repos/registerstuff.cco Repos/headersearch.cco Repos/userdir.cco Repos/busywait.cco Repos/byteorder.cco Repos/rotzooi.cco Repos/timewrap.cco Repos/dotzooi.cco Repos/dayconversion.cco Repos/chain.cco Repos/trackmask.cco Repos/block.cco Repos/circular_buffer.cco Repos/variable_type.cco Repos/netparms.cco Repos/constraints.cco Repos/sciprint.cco Repos/chainstats.cco
	@echo; echo "Building $@"
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)
	chown root:root $@; chmod 04555 $@

tmem: Repos/tmem.cco  Repos/dosyscall.cco  Repos/evlbidebug.cco Repos/hex.cco Repos/bin.cco Repos/bqueue.cco  Repos/timewrap.cco Repos/trackmask.cco Repos/getsok.cco
#tchain: Repos/tchain.cco  Repos/dosyscall.cco  Repos/evlbidebug.cco Repos/hex.cco Repos/bin.cco Repos/bqueue.cco  Repos/timewrap.cco Repos/trackmask.cco Repos/getsok.cco Repos/chain.cco
tchain: Repos/tchain.cco  Repos/dosyscall.cco  Repos/evlbidebug.cco Repos/hex.cco Repos/bin.cco Repos/timewrap.cco Repos/trackmask.cco Repos/getsok.cco Repos/chain.cco
	@echo; echo "Building $@"
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)
tcbuf: Repos/tcbuf.cco Repos/circular_buffer.cco
	$(CXX) -o $@ $^
thdr: Repos/thdr.cco Repos/headersearch.cco Repos/circular_buffer.cco Repos/dosyscall.cco Repos/stringutil.cco
	$(CXX) -o $@ $^
tmask: Repos/tmask.cco Repos/trackmask.cco Repos/dosyscall.cco Repos/hex.cco Repos/bin.cco Repos/variable_type.cco Repos/evlbidebug.cco
	$(CXX) -o $@ $^ $($@_LIBS)
tconstrain: Repos/tconstrain.cco Repos/trackmask.cco Repos/dosyscall.cco Repos/hex.cco Repos/bin.cco Repos/variable_type.cco Repos/constraints.cco Repos/netparms.cco Repos/headersearch.cco Repos/stringutil.cco
	$(CXX) -o $@ $^ $($@_LIBS)

clean:
	-rm -f $(REPOS)/*co $(REPOS)/*fo $(PROGS)
