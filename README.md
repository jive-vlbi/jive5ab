
## jive5ab


The VLBI data recorder software, enabling fast and flexible VLBI data transfers as well as high-speed VLBI data recording. It should compile and run on any POSIX compatible system running on i386 or AMD64 architecture. The reason for the latter is that there is some assembler code in there which is specific to those CPUs.

As of Apr 2019 the code base is git- and cmake-i-fied. This has some consequences (most of them good) for the build process. The options available on the jive5ab `make` command line have, where possible, been ported to `cmake` options. More information follows below.

The source code was also re-organised; documentation now lives in the `./doc/` subdirectory, all scripts in the `./scripts/` subdirectory.

For (limited) backward compatibility the orginal jive5ab [Makefile](src/Makefile) is retained - for those systems that really cannot have any working version of `cmake`. The old [INSTALL](doc/INSTALL) is retained for historical reference and still documents the use of said `Makefile`.

A word about the new build procedure:

- cmake is a Makefile-generator tool. So in stead of diving in and calling `make` there is now a configuration stage first. *Then* call `make`
- cmake generates out-of-source build systems, which means that from the same source tree you can now configure and compile different jive5ab configurations without clashes (think of different C++ compilers, StreamStor libraries, Debug/Release, ...)
- the generated makefiles have a `make install` target. The binary `jive5ab-X-Y-Z` will be installed as well as the most-used scripts `m5copy`, `SSErase.py`, `DirList.py` and `StartJ5`


## The new build procedure

Clone the github repository onto your system

```bash
$> cd /path/to/src
$> git clone https://github.com/jive-vlbi/jive5ab.git
# Of course you can also fork the project on github
# and clone your own version ...
```

The `cmake` workflow is, because it favours out-of-source builds, to create a new directory for each configuration, then `cd` to that directory and execute `cmake` with the appropriate configuration command line options of your desire and let it generate the makefiles for you. When that step finishes succesfully, `make` can be called.

Options are passed to `cmake` like pre-processor directives `-D<OPTION>` or `-D<OPTION>=<VALUE>`, explained below.

In shell speak:
```bash
$> mkdir /path/to/build
$> cd /path/to/build

# Configure this build according to your taste
$> cmake [options] /path/to/src/jive5ab
...

# Now make can be issued, the optional options either speed up (-j NNN) or add verbosity
$> make [-j NNN] [VERBOSE=1]
... (go get coffee or use NNN > 1 to make it quicker)

# It is possible to test/run the binary
$> src/jive5ab/jive5a-X-Y-Z -m 3

# and optionally install it
$> make install
```

### Building jive5ab on the Mark6
The Mark6 comes with an _ancient_ O/S and the cmake version installed is 2.8.2, which causes a compile error when it gets to compiling the assembler code. This can be prevented (fixed) by (re)running the `cmake` configuration step like this:
```bash
$> cmake -DCMAKE_ASM_COMPILER=/usr/bin/gcc -DCMAKE_ASM_FLAGS=-c [other options] /path/to/src/jive5ab
```
Thanks @JonQ for diagnosing and helping with a solution.

## Supported cmake options


|Category | Option | Description |
|:--- | :--- | :--- |
|Debug/Release  | CMAKE_BUILD_TYPE=_Type_  | Substitute 'Release' or 'Debug' for _Type_. Default: Release |
|FiLa10G/Mark5B | FILA10G=ON                | Generate a jive5ab that can *only* record Mark5B data from FiLa10G/RDBE from the `UDPs` protocol|
|SSE Version    | SSE=[20\|41]               | Override automatic 'Streaming SIMD Extensions' version detection (for the assembly code). |
|StreamStor SDK | SSAPI_ROOT=_path_\|nossapi | If not given, searches /usr, /usr/local/src/streamstor, /home/streamstor/Sdk for `libssapi.a`. Otherwise searches _path_. If no StreamStor hardware present (FlexBuff, Mark6) or desired (Mark5*) then you must now *explicitly* pass SSAPI_ROOT=nossapi |
|               | WDAPIVER=_XXXX_    | The StreamStor SDK library version to link with. If not given the system will determine the value itself from whatever is found under `SSAPI_ROOT`. If no libwdapiXXXX.so files are found that's an error. If more than one libwdapXXXX.so are found then WDAPIVER=_XXXX_ *must* be given to select which one is to be used |
|Install location | CMAKE\_INSTALL\_PREFIX=_path_ | The compiled binary will be installed as ${CMAKE\_INSTALL\_PREFIX}/bin/jive5ab-${VERSION}-[32\|64]bit-${BUILD\_TYPE}[-FiLa10G], depending on the configuration details |
|Compiler selection| CMAKE\_C\_COMPILER=[/path/to/]C-compiler | Select the C-compiler to use|
|  | CMAKE\_CXX\_COMPILER=[/path/to/]C++-compiler | Select the C++-compiler to use|


Note that the `B2B=XX` option has disappeared. It is still possible to force a 32-bit build on a bi-arch system, although the procedure is slightly more involved. Execute the `cmake` configuration step like this:

```bash
$> CFLAGS=-m32 CXXFLAGS=-m32 LDFLAGS=-m32 cmake [options] /path/to/jive5ab
```

## Downloads

For very old systems, cmake 2.8.9 (or better .13) can be downloaded and built on the system (the author has good experiences with building cmake 2.8.9 on Debian Wheezy).
Please see this URL for downloading the source code of 2.8.* `cmake` versions: https://cmake.org/files/v2.8/?C=M;O=D



Non-tabular explanation of `cmake` command-line options, preformatted ASCII


    Debug/Release:
        * CMAKE_BUILD_TYPE=<Type>
        Substitute 'Release' or 'Debug' for <Type> (without quotes).
        Default: Release

    StreamStor:
        * Default search path: /usr /usr/local/src/streamstor /home/streamstor/Sdk
          Override with:
              SSAPI_ROOT=/path/to/streamtor
          or
              SSAPI_ROOT=nossapi
          if no streamstor present or required

        * WDAPIVER=XXXX
          request linking agains specific libwdapiXXXX. Default is to let
          the code figure it out by itself.
    

    Install location:
        CMAKE_INSTALL_PREFIX=/path/to/install
        jive5ab will end up as
            ${CMAKE_INSTALL_PREFIX}/bin/jive5ab-${VERSION}-[32|64]bit-${BUILD_TYPE}[-FiLa10G]
        depending on how the build was configured

    Force 32-bit build:
        Call cmake as follows:
        $> CFLAGS=-m32 CXXFLAGS=-m32 cmake -DCMAKE_ASM_FLAGS=-m32 [options] /path/to/src/jive5ab
    
        The weird placement of the -DCMAKE_ASM_FLAGS=-m32 is because cmake does
        not seem to honour the following construction: 
            $> CFLAGS=-m32 CXXFLAGS=-m32 ASFLAGS=-m32 cmake [options] /path/to/src/jive5ab
        nor this one:
            $> CFLAGS=-m32 CXXFLAGS=-m32 CMAKE_ASM_FLAGS=-m32 cmake [options] /path/to/src/jive5ab
            (in this case the CMAKE_ASM_FLAGS gets re-initialized to empty in stead of being propagated).
            Thanks guys!



