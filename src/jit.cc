// implementation
// Copyright (C) 2009-2012 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <jit.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dosyscall.h>
#include <evlbidebug.h>
#include <threadutil.h>

using namespace std;

DEFINE_EZEXCEPT(jit_error)

jit_handle jit_c_compile(const string& code) {
    // Good. Now let's open the compiler and feed sum generated code to it!
    int    tmpfd; 
    char   name[L_tmpnam];
    
    ::snprintf(name, L_tmpnam, "/tmp/jit_XXXXXX"); 
    ASSERT2_POS( (tmpfd=::mkstemp(name)),
                 SCINFO("failed to create tempfilename for JustInTime compiling")  );
    ::close(tmpfd);
    // Do not make this a fatal exception
    if( ::unlink(name)==-1 ) {
        DEBUG(-1, "**** WARNING: JustInTime compilation failed to remove tmpfile" << endl <<
                  "****   '" << name << "' - " << evlbi5a::strerror(errno) << endl);
    }

    // With high enough debug level output the generated code
    DEBUG(5, "**** JIT - attempt to compile the following code:" << endl << code << endl);

    void*         tmphandle;
    FILE*         fptr;
    const string  generated_filename(name);
    const string  obj( generated_filename + ".o" );
    const string  lib( generated_filename + SOEXT );
    ostringstream compile;
    ostringstream link;

    // Let the compiler read from stdin ...
    compile << "gcc" << BOPT << " -fPIC -g -c -Wall -O3 -x c -o " << obj << " -";
    DEBUG(3, "jit_c_compile: " << compile.str() << endl);
    ASSERT2_NZERO( (fptr=::popen(compile.str().c_str(), "w")),
                   SCINFO("popen('" << compile.str() << "' fails - " 
                            << evlbi5a::strerror(errno)) );
    // Allright - compiler is online, now feed the code straight in!
    ASSERT_COND( ::fwrite(code.c_str(), 1, code.size(), fptr)==code.size() );
    // Close the pipe and check what we got back
    ASSERT_COND( ::pclose(fptr)==0 );

    // Now produce a loadable thingamabob from the objectcode
    link << "gcc" << BOPT << LOPT << " -fPIC -o " << lib << " " << obj;
    DEBUG(3, "jit_c_compile: " << link.str() << endl);
    ASSERT_ZERO( ::system(link.str().c_str()) );

    // Now delete the tmp object file
    ASSERT_ZERO( ::unlink(obj.c_str()) );

    // Huzzah! Compil0red and Link0red.
    // Now all that's needed is loading
    EZASSERT2_NZERO( (tmphandle=::dlopen(lib.c_str(), RTLD_GLOBAL|RTLD_NOW)),
                     jit_error,
                     EZINFO(::dlerror() << " opening " << lib << endl) );
    return jit_handle(tmphandle, lib);
}

jit_handle::jit_handle():
    impl( new jit_handle_impl() )
{}

jit_handle::jit_handle(void* h, const string& f):
    impl( new jit_handle_impl(h, f) )
{}



jit_handle::jit_handle_impl::jit_handle_impl():
    handle( 0 )
{}

jit_handle::jit_handle_impl::jit_handle_impl(void* h, const string& f):
    handle( h ), dllname( f )
{ EZASSERT_NZERO(handle, jit_error); EZASSERT(dllname.empty()==false, jit_error); }

jit_handle::jit_handle_impl::~jit_handle_impl() {
    if( handle && ::dlclose(handle)==-1 )
        DEBUG(-1, "Failed to close DLL '" << dllname << "' - " << ::dlerror() << endl);

    if( !dllname.empty() )
        if( ::unlink(dllname.c_str())==-1 )
            DEBUG(-1, "Failed to remove tmp DLL '" << dllname << "' - " << evlbi5a::strerror(errno) << endl);
}

