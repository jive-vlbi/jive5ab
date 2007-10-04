# Makefile to create (local) versions of the Mark5* s/w suite w/o
# overwriting existing versions. The binaries will be placed in the
# same directory as this makefile is in

# There should be little need for changing this file;
# only if eg:
#   * new streamstor libssapi.a [for eg different gcc version]
#   * path-to-sources changes [CMASTER/FMASTER for C/Fortran source]


CC=gcc -g -Wall -W -Werror -malign-double -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
CXX=g++ -g -Wall -W -Werror -malign-double -D_REENTRANT -pthread -D_POSIX_PTHREAD_SEMANTICS
FC=g77 -g

# StreamStor stuff
SSAPIROOT=/home/streamstor/Sdk
SSAPIINCD=$(SSAPIROOT)/include

# The actual librarypath is dependant on your
# gcc version
#SSAPILIBD = $(SSAPIROOT)/lib/gcc_v2  # gcc 2.*
SSAPILIBD = $(SSAPIROOT)/lib/gcc_v3  # gcc 3.*
SSAPILIBS = -lssapi -lwdapi801 -lm

# hjlib
HJROOT=/root/evlbi
HJINCD=$(HJROOT)/include
HJLIBD=$(HJROOT)/lib
HJLIBS=-lhj

GLIBINCD=$(shell glib-config --cflags | sed '/command not/d')
ifeq ($(GLIBINCD),)
	GLIBINCD=$(shell pkg-config --cflags glib-2.0 | sed '/command not/d')
endif
#INCD=-I$(SSAPIINCD) $(GLIBINCD) -I$(HJINCD)
INCD=-I$(SSAPIINCD) -I. $(GLIBINCD) 

# All libraries that you need to linx0r in to have
# a working proggie
# they are app specific 
#Mark5A_LIBS=-L$(SSAPILIBD) -L$(HJLIBD) -lcurses -lpthread $(SSAPILIBS) $(HJLIBS) -lm
Mark5A_LIBS=-L$(SSAPILIBD) -L$(HJLIBD) -lcurses -lpthread $(SSAPILIBS) -lm
tstMark5A_LIBS=-lreadline -lcurses
test_LIBS=-L$(SSAPILIBD) $(SSAPILIBS) -lm -lnsl -lpthread

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
PROGS=Mark5A tstMark5A test

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
	chown root:root $@; chmod 04755 $@

tstMark5A: $(foreach O, $(patsubst %.c, %.co, $(patsubst %.f, %.fo, $(tstMark5A_SRC))), $(addprefix $(REPOS),$(O))) 
	@echo; echo "Linking $@"; echo
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)
	chown jball:users $@

test: Repos/test.cco Repos/ioboard.cco Repos/dosyscall.cco Repos/xlrdevice.cco Repos/evlbidebug.cco Repos/playpointer.cco Repos/transfermode.cco Repos/runtime.cco Repos/stringutil.cco Repos/streamutil.cco Repos/hex.cco Repos/bin.cco
	@echo; echo "Building test"
	$(CXX) -o $@ $^ $(LIBD) $(LIBS) $($@_LIBS)

clean:
	-rm -f $(REPOS)/*co $(REPOS)/*fo $(PROGS)
