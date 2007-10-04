#include <bin.h>

using namespace std;

namespace bin_lcl {
    // the exception's member function
    const char* unrecognized_byteorder::what( void ) const throw() {
        return "bin.h: Unrecognized byte-order!?";
    }

    // the byte-order-test struct
    botest_t::botest_t() {
        __t.i = 0x1;
        // see where the byte ends up
        if( __t.c[sizeof(unsigned int)-1]==0x1 )
            endianness = big;
        else if( __t.c[0]==0x1 )
            endianness = little;
        else
            throw unrecognized_byteorder();
    }

    // the iterator
    iter_t::iter_t() :
        cur(0), inc(0)
    {}
    iter_t::iter_t(const byte* p, ptrdiff_t incr) :
        cur(p), inc(incr)
    {}
    const iter_t& iter_t::operator++( void ) {
        cur += inc;
        return *this;
    }
    const iter_t& iter_t::operator++( int ) {
        cur += inc;
        return *this;
    }
    const byte& iter_t::operator*( void ) const {
        return *cur;
    }
    bool iter_t::operator==( const iter_t& other ) const {
        return (cur!=0 && (cur==other.cur));
    }
    bool iter_t::operator!=( const iter_t& other ) const {
        return !(this->operator==(other));
    }

    // the bininterface_t   
    bininterface_t::~bininterface_t() {}

} // end of namespace bin_lcl


// this is what the user sees/uses
bin_t::bin_t( const bin_t& o ) :
    binptr( o.binptr->clone() )
{}

bin_t::~bin_t() {
    delete binptr;
}

ostream& operator<<( ostream& os, const bin_t& ht ) {
	ht.binptr->print_in_bin(os);
	return os;
}
