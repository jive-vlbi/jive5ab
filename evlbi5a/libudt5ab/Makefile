# Makefile from udt4 edited by HV to support compilation
# under jive5ab
# jive5ab's makefile calls this Makefile with
#    CPP=<whatever it is compiled with>
#    B2B=32|64  which bit-flavour jive5ab is compiled with
# 
# NOTE: the original makefile had 'C++' but you can't, apparently,
#       pass that in on the commandline as "make C++=/path/to/compiler"
# The os setting is figured out by this Makefile itself


## Detect the operating system
UNAME=$(shell uname)
ifeq ($(UNAME),Linux)
	os     = LINUX
	target = libudt5ab.a
endif

ifeq ($(UNAME),Darwin)
	os     = OSX
	target = libudt5ab.a
endif

ifeq ($(UNAME),SunOS)
    os     = BSD
    target = libudt5ab.a
endif

ifeq ($(os),)
	target=failos
endif

## Which architecture
ifeq ($(B2B),32)
   arch = IA32
endif

ifeq ($(B2B),64)
	arch = AMD64
endif

ifeq ($(arch),)
	target=failarch
endif

CCFLAGS = -fPIC -Wall -Wextra -D$(os) -finline-functions -O3 -fno-strict-aliasing #-msse3

ifeq ($(arch), IA32)
   CCFLAGS += -DIA32
endif

ifeq ($(arch), POWERPC)
   CCFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), SPARC)
   CCFLAGS += -DSPARC
endif

ifeq ($(arch), IA64)
   CCFLAGS += -DIA64
endif

ifeq ($(arch), AMD64)
   CCFLAGS += -DAMD64
endif

OBJS = md5.o common.o window.o list.o buffer.o packet.o channel.o queue.o ccc.o cache.o core.o epoll.o api.o

all: $(target) 

%.o: %.cpp %.h udt.h
	$(CPP) $(CCFLAGS) $< -c

libudt5ab.a: $(OBJS)
	ar -rcs $@ $^

#libudt5ab.so: $(OBJS)
#	$(CPP) -shared -o $@ $^

#libudt5ab.dylib: $(OBJS)
#	$(CPP) -dynamiclib -o $@ -lstdc++ -lpthread -lm $^

clean:
	rm -f *.o *.so *.dylib *.a udt

.PHONY: failos failarch
failos:
	@echo "Unrecognized/unset operating system!"; exit 1

failarch:
	@echo "Unrecognized/unset architecture!"; exit 1

info:
	@echo "$(UNAME) => os=$(os) arch=$(arch) target=$(target)"
	@echo " compiler=$(CPP)"
	@echo "MAKECMDGOALS=$(MAKECMDGOALS)"
