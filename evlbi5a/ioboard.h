// access the mark5 ioboard

#ifndef EVLBI5A_IOBOARD_H
#define EVLBI5A_IOBOARD_H

#include <string>
#include <exception>
#include <map>
#include <iostream>
#include <sstream>
#include <vector>

#include <hex.h>


// an ioboard exception
struct ioboardexception:
    public std::exception
{
    ioboardexception( const std::string& m );

    virtual const char* what( void ) const throw();
    virtual ~ioboardexception( void ) throw();

    const std::string message;
};


// helper for creating masks with n bits set
// Will create an array-of-Ts with in each position n
// a mask with n bits set for up to sizeof(T)*8 positions
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
        throw ioboardexception(e.str());
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
    regdesc_type( unsigned int nb, unsigned int s, unsigned int w ):
        nbit( nb ), startbit( s ), word( w )
    {
        // check sanity of args
        if( nbit==0 )
            throw ioboardexception("0bit-sized register definition not allowed");

        if( startbit+nbit>__maxbit ) {
            std::ostringstream  e;
            e << "bitaddress out-of-range: start+nbit>maxbit ("
                << startbit << "+" << nbit << ">" << __maxbit << ")";
            throw ioboardexception(e.str());
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
        // only allow creation from pointer + regdesc_type to enforce
        // usefull construction
        reg_pointer( const regdesc_type<T>& regdesc, volatile T* baseptr ):
            wordptr( baseptr+regdesc.word ), startbit( regdesc.startbit ),
            valuemask( bitmasks<T>()[regdesc.nbit] ), fieldmask( valuemask<<startbit )
        {
            // make sure it's a non-zerolength bitfield
            if( regdesc.nbit==0 )
                throw ioboardexception("Attempt to create reg_pointer from zero-bitlength field!");
        }

        // depending on how this object is used we either read or
        // write a value from/to the actual H/W

        // assignment to this object will write into the h/w.
        // (possibly) truncate value and shift to correct position,
        // do it in a read-modify-write cycle; it's the only thing we
        // can sensibly do, right?
        const reg_pointer<T>& operator=( const T& t ) {
            *wordptr = ((*wordptr&(~fieldmask))|((t&valuemask)<<startbit));
            return *this;
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
        const T      valuemask; // to truncate an assigned value before writing to h/w
        const T      fieldmask; // the mask, shifted to the position in the register
};

// the mark5a I/O board registers
struct mk5areg {
    // size of region to map
    // cf IOBoard.c Mk5A ioboard needs to map 128 bytes into memory
    static const size_t   mmapregionsize = 128;

    // The FQ_UD bit is wired to the AD9850 'FQ_UD' pin, the 'W_CLK' to
    //  the AD9850's W_CLK [see .pdf], the 'W' 8-bit value is connected
    //  to the 8-bit parallel load register "W[0:4]" for loading the
    //  control/data word into the AD9850
    enum ipb_regname {
        invalid_ipmk5a, notClock, W_CLK, U, FQ_UD=U, mode, vlba, errorbits, R, W
        // aliases for full words
        // words 3 and 5 only contain ReadOnly fields so we don't bother
        // doing them
        ,ip_word0, ip_word1, ip_word2 /*, ip_word3*/, ip_word4 /*, ip_word5*/
    };
    // the register names for the outputboard
    // their locations etc will be keyed to this
    // 'V' => VLBA  'I' => Internal clock
    enum opb_regname {
       invalid_opmk5a, ChBSelect, ChASelect, F, I,  AP, AP1, AP2,
       V, SF, CODE, C, Q, S, NumberOfReSyncs, DIMRev, FillPatMSBs, FillPatLSBs
       // aliases for full words
       //,op_word0, op_word1, op_word2, op_word3, op_word4
    };

    // the mk5a ioboard uses 16bit registers
    typedef regdesc_type<unsigned short>   regtype;
    // now define a mapping between the symbolic names and where the regs
    // actually are located
    typedef std::map<ipb_regname, regtype> ipb_registermap;
    typedef std::map<opb_regname, regtype> opb_registermap;

    // get read-only access to the registerfile
    static const ipb_registermap&  ipb_registers( void );
    static const opb_registermap&  opb_registers( void );
};

// print out the registernames in hrf:
std::ostream& operator<<(std::ostream& os, mk5areg::ipb_regname r );
std::ostream& operator<<(std::ostream& os, mk5areg::opb_regname r );





// and now, without further ado... *drumroll* ... the ioboard interface!
// You can create as many of these as you like; it's implemented as a singleton
// which will attempt to initialize at first access. If something's fishy,
// an exception will be thrown
class ioboard_type {
    public:

        // the kinds of boards we recognize
        // in case of mk5b, we can have DOM or DIM
        enum board_type {
            unknown_boardtype, mk5a, mk5b
        };

        // only feature default c'tor. Will go and look which 
        // kind of board can be found. Throws if nothing can be
        // found
        ioboard_type();

        board_type      boardType( void ) const;

        // Get the input/outpdesign revisions
        unsigned short  idr( void ) const;
        unsigned short  odr( void ) const;

        // access mk5a registers
        typedef reg_pointer<mk5areg::regtype::register_type> mk5aregpointer;

        mk5aregpointer operator[]( mk5areg::ipb_regname rname ) const;
        mk5aregpointer operator[]( mk5areg::opb_regname rname ) const;

        void dbg( void ) const;

        // doesn't quite do anything...
        ~ioboard_type();

    private:
        // How many references to the ioboard?
        // if the destructor detects the refcount going
        // to zero the state will be returned to "uninitialized"
        // (thus having the ability to do a proper cleanup)
        static unsigned long long int   refcount;
        // the boardtype
        static board_type               boardtype;

        // pointers to the boardregions [input and output]
        // Eithter both are zero [ie the ioboard subsystem not
        // initialized OR they are both nonzero so testing
        // just one for existance is good enough for initializationness
        // testing]
        static unsigned short*          inputdesignrevptr;
        static unsigned short*          outputdesignrevptr;

        // These be memorymapped regions. Keep track of
        static volatile unsigned short* ipboard;
        static volatile unsigned short* opboard;

        // Do the initialization. Note: will be done unconditionally.
        // It's up the the caller to decide wether or not to call this
        // function. Will throw up in case of fishyness.
        void                            do_initialize( void );

        typedef std::vector<unsigned long> pciparms_type;
        // board-specific initializers. they're supposed to set up the boards
        // and fill in the pointers to the registers. They also work
        // unconditionally. It is the callers responsibility to (only) call them
        // when appropriate
        void                            do_init_mark5a( const pciparms_type& pci );
        void                            do_init_mark5b( const pciparms_type& pci );

        // and the cleanup routines. Will be called automagically by
        // the destructor iff nobody references the ioboard anymore
        // and by looking at the boardtype
        void                            do_cleanup_mark5a( void );
        void                            do_cleanup_mark5b( void );

        // Prohibit these because it is sematically nonsense,
        // even though technically it would not be a problem
        // to allow copy/assignment.
        ioboard_type( const ioboard_type& );
        const ioboard_type& operator=( const ioboard_type& );

};


std::ostream& operator<<( std::ostream& os, ioboard_type::board_type bt );

#endif // includeguard
