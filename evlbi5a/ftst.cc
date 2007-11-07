#include <iostream>
#include <flagstuff.h>

using namespace std;


enum iob_flags {
    mk5a, mk5b, dim, dom, blah
};

enum ioc_flags {
    mk5c
};

typedef flagset_type<iob_flags, unsigned int>  iobflags_type;
typedef iobflags_type::flag_map_type           iobflagmap_type;
typedef iobflags_type::flag_descr_type         iobflag_descr;

iobflagmap_type mkiobflags_map( void ) {
    iobflagmap_type rv;

    rv.insert( make_pair(mk5a, iobflag_descr(0x1, "Mark5A")) );
    rv.insert( make_pair(mk5b, iobflag_descr(0x2)) );

    cout << "test: attempt to insert 0 flag (should fail) ... ";
    try {
        rv.insert( make_pair(blah, iobflag_descr(0)) );
        cout << "ok?!?!" << endl;
    }
    catch( const exception& e ) {
        cout << "got exception [" << e.what() << "]" << endl;
    }
    // setting the dim flag implies also setting the
    // mark5b flag ...
    rv.insert( make_pair(dim, iobflag_descr(0x80|0x2, "Mark5B/DIM")) );

    return rv;
}


int main() {

    try {
        // initialize set of known flags
        iobflags_type::set_flag_map( mkiobflags_map() );


        iobflags_type        iobflags;

        cout << "1) iobflags=" << iobflags << endl;
        iobflags|=mk5a;
        cout << "2) iobflags=" << iobflags << endl;
        if( iobflags&mk5a )
            cout << "Mk5A flag is set" << endl;
        cout << "2b) iobflags&mk5a=" << (iobflags&mk5a) << endl;
        cout << "3) iobflags|mk5b=" << (iobflags|mk5b) << endl;
        cout << "3b) (iobflags|mk5b)&mk5b =" << ((iobflags|mk5b)&mk5b) << endl;
        cout << "3c) (iobflags|mk5b)&mk5a =" << ((iobflags|mk5b)&mk5a) << endl;
        cout << "clr(mk5a).set(dim)" << endl;
        iobflags.clr(mk5a).set(dim);
        cout << "4) iobflags=" << iobflags << endl;
        cout << "4b) iobflags&mk5b=" << (iobflags&mk5b) << endl;
        cout << "4c) iobflags&dim=" << (iobflags&dim) << endl;


        cout << "Attempt to set unknown flag ... ";
        try {
            iobflags|=dom;
            cout << "ok?!" << endl;
        }
        catch( const exception& e ) {
            cout << e.what() << endl;
        }
    }
    catch( const exception& e ) {
        cout << "!!!!! " << e.what() << endl;
    }
    catch( ... ) {
        cout << "Caught unknown exception :(" << endl;
    }

    return 0;
}

