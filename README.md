
$> cd /path/to/src
$> git checkout jive5ab.git

$> mkdir /path/to/build
$> cd /path/to/build
$> cmake [options] /path/to/src/jive5ab
...
    => to force 32-bit build:
    $> CFLAGS=-m32 CXXFLAGS=-m32 cmake [options] /path/to/src/jive5ab

    Debug/Release:
    Substitute 'Release' or 'Debug' for <Type> in this (without quotes):
    $> cmake -DCMAKE_BUILD_TYPE=<Type> [other options] /path/to/src/jive5ab
    

    StreamStor:
    Default search path: /usr /usr/local/src/streamstor /home/streamstor/Sdk
    Override with: -DSSAPI_ROOT=/path/to/streamtor or -DSSAPI_ROOT=nossapi
    if no streamstor present or required

$> make [-j NNN] 
...

[$> make install]


