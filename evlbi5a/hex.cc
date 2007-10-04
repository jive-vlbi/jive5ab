// implementation of non-templated classes
#include <hex.h>

using namespace std;

namespace hex_lcl {
    // the exception
    unrecognized_byteorder::unrecognized_byteorder() :
        std::exception()
    {}

    const char* unrecognized_byteorder::what( void) const throw() {
        return "hex.h: Unrecognized byteorder";
    }
    unrecognized_byteorder::~unrecognized_byteorder() throw()
    {}

    // The byte-order-test struct
    botest_t::botest_t() {
        __t.i = 0x1;
        // see where the byte ends up
        if( __t.c[sizeof(unsigned int)-1]==0x1 ) {
            endianness = big;
        } else if( __t.c[0]==0x1 ) {
            endianness = little;
        } else {
            throw unrecognized_byteorder();
        }
    }

    // the byte-iterator
    _iter_t::_iter_t() :
        cur(0), inc(0)
    {}
    _iter_t::_iter_t(const byte* p, ptrdiff_t inc) :
        cur(p), inc(inc)
    {}
    const _iter_t& _iter_t::operator++( void ) {
        cur += inc;
        return *this;
    }
    const _iter_t& _iter_t::operator++( int ) {
        cur += inc;
        return *this;
    }
    const byte& _iter_t::operator*( void ) const {
        return *cur;
    }
    bool _iter_t::operator==( const _iter_t& other ) const {
        return (cur!=0 && (cur==other.cur));
    }
    bool _iter_t::operator!=( const _iter_t& other ) const {
        return !(this->operator==(other));
    }


    // hexinterface
    hexinterface_t::~hexinterface_t()
    {}
}

// and the hex_t itself
hex_t::hex_t( const hex_t& ht ):
    hexptr( ht.hexptr->clone() )
{}

hex_t::~hex_t() {
    delete hexptr;
}

ostream& operator<<(ostream& os, const hex_t& ht) {
    ht.hexptr->print_in_hex(os);
    return os;
}
