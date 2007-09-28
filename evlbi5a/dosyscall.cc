// implementation
#include <dosyscall.h>

#include <errno.h>
#include <string.h>


using namespace std;

//
//  The 'last system-error type'
//
lastsyserror_type::lastsyserror_type():
    sys_errno( errno ), sys_errormessage( ((sys_errno!=0)?(::strerror(sys_errno)):("<success>")) )
{}

// if errno == 0, don't show anything
ostream& operator<<( ostream& os, const lastsyserror_type& lse ) {
    if( lse.sys_errno!=0 )
        os << " - " << lse.sys_errormessage << "(" << lse.sys_errno << ")";
    return os;
}

//
// the exception
//
syscallexception::syscallexception( const string& s ):
    msg( s )
{}

const char* syscallexception::what() const throw() {
    return msg.c_str();
}
syscallexception::~syscallexception() throw()
{}


