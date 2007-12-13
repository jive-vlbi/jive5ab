// wrappers for accessing memory-mapped registers
#ifndef JIVE5A_REGISTERSTUFF_H
#define JIVE5A_REGISTERSTUFF_H

#include <ezexcept.h>

// Define exceptions that might be thrown
DECLARE_EZEXCEPT(bitmask_exception);
DECLARE_EZEXCEPT(regdesc_exception);
DECLARE_EZEXCEPT(regpointer_exception);


// helper for creating masks with n bits set
// Will create an array-of-Ts with in each position n
// a mask with bits 0:n set for up to sizeof(T)*8 positions
template <typename T>
struct bitmasks {
    static const unsigned int  nmasks = sizeof(T)*8+1;
    bitmasks() {
        if( !masks ){
            T    curmask = 0;

            masks = new T[nmasks];
            for( unsigned int i=0; i<nmasks; ++i ) {
                masks[ i ] = curmask;
                curmask = (curmask<<1)|0x1;
            }
        }
    }

    // by making this non-static we enforce the user
    // to always have a variable of this type and thus
    // we do auto-initialization: not used=>no initialization!
    T operator[]( unsigned int idx ) const {
        if( idx<nmasks )
            return masks[idx];
        std::ostringstream  e;
        e << "Attempt to access mask#" << idx << " whilst " << nmasks << " is the limit";
        throw bitmask_exception(e.str());
    }
        
    private:
        static T* masks;
};
// static non-const datamembers for templates can be defined here (in a header file, that is)
// - if we do this for non-template thingies then we get multiple defined symbols
template <typename T>
T* bitmasks<T>::masks = 0;


// generic register and/or bitset description
// Describes an nbit-bit value, starting at bit startbit
// in word 'word' (index). Usually offset is wrt some baseadress
// but that's not important for *this* part.
// The template parameter is the base-type for a full register
// - eg a 16bit value for the mark5a. Its type is used to
// check that the nbit/startbit/offset make sense.
template <typename T>
struct regdesc_type {
    // maximum bitaddress for the given type
    static const unsigned int __maxbit = (sizeof(T)*8);

    // allow reinterpretation of 'pointer-to-base' as an array of
    // equal sized registers of the current type
    typedef T   register_type;

    // default. Can be recognized by a zero nbit field.
    // The 'full' c'tor does not allow a zero nbit field
    regdesc_type():
        nbit( 0 ), startbit( 0 ), word( 0 )
    {}

    // Shortcut for registers which are a full word,
    // namely word # 'w'
    regdesc_type( unsigned int w ):
        nbit( __maxbit ), startbit( 0 ), word( w )
    {}

    // Create an 'nbit' sized bitfield, starting at bit 's' in 
    // word # 'w'
    regdesc_type( unsigned int nb, unsigned int s, unsigned int w ):
        nbit( nb ), startbit( s ), word( w )
    {
        // check sanity of args
        if( nbit==0 )
            throw regdesc_exception("0bit-sized register definition not allowed");

        if( startbit+nbit>__maxbit ) {
            std::ostringstream  e;
            e << "bitaddress out-of-range: start+nbit>maxbit ("
                << startbit << "+" << nbit << ">" << __maxbit << ")";
            throw regdesc_exception(e.str());
        }
    }

    // the properties
    unsigned int   nbit;
    unsigned int   startbit;
    unsigned int   word;
};


// An actual 'bound' register. Will allow r/w to the field in the 
// register. Will take care of masking/shifting etc
template <typename T>
struct reg_pointer {
    public:
        // sometimes it *is* handy to be able to create an empty reg_pointer.
        // It's up to the code to make sure you don't de-reference it.
        // (well, if you do, you get a SEGFAULT ...)
        reg_pointer():
            wordptr( 0 ), startbit( (unsigned int)-1 ),
            valuemask( 0 ), fieldmask( 0 )
        {}
        // only allow creation from pointer + regdesc_type to enforce
        // usefull construction
        reg_pointer( const regdesc_type<T>& regdesc, volatile T* baseptr ):
            wordptr( baseptr+regdesc.word ), startbit( regdesc.startbit ),
            valuemask( bitmasks<T>()[regdesc.nbit] ), fieldmask( valuemask<<startbit )
        {
            // make sure it's a non-zerolength bitfield
            if( regdesc.nbit==0 )
                throw regpointer_exception("Attempt to create reg_pointer from zero-bitlength field!");
        }

        // depending on how this object is used we either read or
        // write a value from/to the actual H/W

        // assignment to this object will write into the h/w.
        // (possibly) truncate value and shift to correct position,
        // do it in a read-modify-write cycle; it's the only thing we
        // can sensibly do, right?
        //
        // By letting ppl assign arbitrary types, we have at least
        // the possibility to specialize for 'bool'.
        // If you assign a 32bit value to a 16bit hardware register ...
        // well ... too bad!
        template <typename U>
        const reg_pointer<T>& operator=( const U& u ) {
            *wordptr = ((*wordptr&(~fieldmask))|((T(u)&valuemask)<<startbit));
            return *this;
        }

        // Have a specialized fn for assigning bool: make sure that
        // bit 0 is set if b==true ...
        const reg_pointer<T>& operator=( const bool& b ) {
            T    value( (b)?(0x1):(0x0) );
            // forward to normal operator=()
            return this->operator=(value);
        }

        // and also implement the dereference operator
        T operator*( void ) const {
            return (((*wordptr)&fieldmask)>>startbit);
        }
        // using this object as the underlying type (usually
        // when used as rval) will read the value from the h/w
        operator T( void ) const {
            return *(*this);
            //return (((*wordptr)&fieldmask)>>startbit);
        }

        friend std::ostream& operator<<(std::ostream& os, const reg_pointer<T>& rp ) {
            os << "value @bit" << rp.startbit << " [vmask=" << hex_t(rp.valuemask)
                << " fmask=" << hex_t(rp.fieldmask) << "]";
            return;
        }

    private:
        // pointer to the full word where this value is located in
        volatile T*  wordptr;
        unsigned int startbit;
        // precomputed masks
        T            valuemask; // to truncate an assigned value before writing to h/w
        T            fieldmask; // the mask, shifted to the position in the register
        //const T      valuemask; // to truncate an assigned value before writing to h/w
        //const T      fieldmask; // the mask, shifted to the position in the register
};

#endif
