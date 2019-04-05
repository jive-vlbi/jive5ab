
$> cd /path/to/src
$> git checkout jive5ab.git

$> mkdir /path/to/build
$> cd /path/to/build
$> cmake [options] /path/to/src/jive5ab
...
    Debug/Release ("BUILD_TYPE"):
    Substitute 'Release' or 'Debug' for <Type> in this (without quotes):
    $> cmake -DCMAKE_BUILD_TYPE=<Type> [other options] /path/to/src/jive5ab
    

    StreamStor:
    Default search path: /usr /usr/local/src/streamstor /home/streamstor/Sdk
    Override with:
        -DSSAPI_ROOT=/path/to/streamtor
    or
        -DSSAPI_ROOT=nossapi
    if no streamstor present or required

    Install location:
        -DCMAKE_INSTALL_PREFIX=/path/to/install
    jive5ab will end up as
        /path/to/install/bin/jive5ab-${VERSION}-[32|64]bit-${BUILD_TYPE}[-FiLa10G]
    depending on how the build was configured

    => to force 32-bit build:
    $> CFLAGS=-m32 CXXFLAGS=-m32 cmake -DCMAKE_ASM_FLAGS=-m32 [options] /path/to/src/jive5ab
    
    The weird placement of the -DCMAKE_ASM_FLAGS=-m32 is because cmake does
    not seem to honour the following construction: 
        $> CFLAGS=-m32 CXXFLAGS=-m32 ASFLAGS=-m32 cmake [options] /path/to/src/jive5ab
    nor this one:
        $> CFLAGS=-m32 CXXFLAGS=-m32 CMAKE_ASM_FLAGS=-m32 cmake [options] /path/to/src/jive5ab
        (in this case the CMAKE_ASM_FLAGS gets re-initialized to empty in stead of being propagated).
        Thanks guys!


$> make [-j NNN] 
...

[$> make install]


