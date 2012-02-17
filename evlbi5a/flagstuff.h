// generic enum -> bitflag(s) mapping.
// Copyright (C) 2007-2008 Harro Verkouter
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
//
// Provides consistency and safety: users only
// can use defined enumeration values; there can be
// no confusion between the type of the enum and the 
// actual bit(s) that it maps to
#ifndef JIVE5A_FLAGSTUFF_H
#define JIVE5A_FLAGSTUFF_H

#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <exception>


// exceptions thrown
// Note: we make them templates - seems a bit over-the-top
//       but it allows us to get away with multiple instantiations
//       i.e. we don't have to split up this code in a .cc and .h
//       file. If not templated, then everyting but this exception
//       is a template so the user would have to compile&link the
//       .cc file into his/her app. Now, it being a template too,
//       usr can just #include<> and be done with it!
#define DEFINEEXCEPTION(xept) \
    template <typename T = void> \
    struct xept:\
        public std::exception\
    {\
        virtual const char* what( void ) const throw() {\
            return #xept;\
        }\
    };
    


// creating a flag with no bits set is prolly a logic error
DEFINEEXCEPTION(empty_bitset)
// thrown when trying to set/inspect a flag which is not found in
// the mapping
DEFINEEXCEPTION(enum_not_in_map)
// thrown when attempt to multiple initialize the map and
// your code said it should not be
DEFINEEXCEPTION(multiple_map_initialization)

// return a string representation in hex of
// the given value
template <typename F>
std::string hexifier( const F& f ) {
    std::ostringstream s;
    s << "0x" << std::hex << f << std::dec;
    return s.str();
}

// this describes an actual flag:
// the bit(s) to be set and the string description.
// When the bit(s) are set then, when displaying in
// HumanReadableFormat, the name will be displayed.
// It is templated on the base-type for the bits
template <typename F>
struct flagdescr_type {
    // we require at least a bitset. A name *may* be provided
    // if it's not, we default to the ascii representation
    // of the bitset.
    flagdescr_type( const F& f, const std::string& nm = std::string() ):
        __f( f ), __nm( ((nm.empty())?(hexifier(f)):(nm)) ) 
    {
        // no bits set is rly weird?
        if( __f==(F)0 )
            throw empty_bitset<>();
    }

    // properties
    const F           __f;
    const std::string __nm;
};



// template parameters are the enum-type E
// and the base-type for flags (int, unsigned short, ...), F.
// Note: all instances of a particular flagset_type<> share
//       the same set of recognized flags. You're free to
//       modify the mapping at runtime - just be aware that
//       it may affect setting/clearing of flags for *all*
//       instances of that type, existing and yet-to-be created.
// Since it's a static datamember, you have the choice to allow
// multiple initialization of this mapping of recognized flags.
// By adding it to the template-parameter-list you get a different
// type for multiple/singularly initializable objects, which is
// obviously much safer than, eg. specifying it on a per-call basis.
template <typename E, typename F, bool AllowMultipleInit=false>
class flagset_type {
    public:
        // provide a typedef for the flagdescr_type
        typedef flagdescr_type<F>               flag_descr_type;
        // the map type for mapping enum => descriptor
        typedef std::map<E, flagdescr_type<F> > flag_map_type;


        // Constructor.
        // By default no bits are set in the flags.  
        // Note: it *is* possible to set bits that are
        //       unknown to the system, 's up to you.
        //       When printing, those unknown flags are 
        //       dealt with gracefully (that is: they're
        //       ignored ...).
        flagset_type( const F initf = (F)0 ):
            flags( initf )
        {}

        // copy c'tor
        flagset_type( const flagset_type<E,F,AllowMultipleInit>& other ):
            flags( other.flags )
        {}

        // and assignment
        const flagset_type<E,F>& operator=( const flagset_type<E,F,AllowMultipleInit>& other ) {
            if( this!=&other )
                flags = other.flags;
            return *this;
        }

        // Modify flags. The functions return a non-const
        // ref to the object itself so you can easily
        // chain modifications like:
        // flagset_type<>    fst;
        // fst.clr( flag2 ).set( flag1 ).set( flagn );

        // set the indicated flag. throw if flag not defined.
        flagset_type<E, F, AllowMultipleInit>& set( const E& e ) {
            typename flag_map_type::const_iterator  curf = flag_map.find(e);

            if( curf==flag_map.end() )
                throw enum_not_in_map<>();

            // set the associated bits
            flags = (F)(flags | curf->second.__f);
            return *this;
        }
        // clear the indicated flag.
        flagset_type<E, F>& clr( const E& e ) {
            typename flag_map_type::const_iterator  curf = flag_map.find(e);

            if( curf==flag_map.end() )
                throw enum_not_in_map<>();

            // clear the associated bits
            flags &= ~curf->second.__f;
            return *this;
        }
        // clear all flags
        flagset_type<E,F,AllowMultipleInit>& clr_all( void ) {
            flags = (F)0;
            return *this;
        }
        // test if *any* flags are set
        bool empty( void ) const {
            return (flags==(F)0);
        }

        // return true if flag is set ...
        bool is_set( const E& e ) const {
            typename flag_map_type::const_iterator  curf = flag_map.find(e);

            if( curf==flag_map.end() )
                throw enum_not_in_map<>();
            return ((flags&curf->second.__f)==curf->second.__f);
        }

        // the bitwise OR/AND overloads are merely provided as
        // syntactic sugah.

        // in-place modification, set the flag 'e'
        flagset_type<E, F,AllowMultipleInit>& operator|=( const E& e ) {
            return this->set( e );
        }

        // return new instance which is the OR of this + indicated flag
        flagset_type<E, F,AllowMultipleInit>  operator|( const E& e ) const {
            flagset_type<E,F,AllowMultipleInit>   rv( *this );
            rv.set( e );
            return rv;
        }

        // bitwise AND is equivalent to testing if said flag is set
        bool operator&( const E& e ) const {
            return this->is_set(e);
        }

        // Set the map of recognized flags.
        // Depending on the value of the "AllowMultipleInit"
        // template parameter we allow multiple initializations
        // or not
        static void set_flag_map( const flag_map_type& m ) {
            if( flag_map.size()>0 && AllowMultipleInit==false )
                throw multiple_map_initialization<>();
            flag_map = m;
            return;
        }

        // RO-access to the set of defined mappings
        static const flag_map_type& get_flag_map( void ) {
            return flag_map;
        }
        // if need be, we could implement finergrained control over
        // the mapping, eg, erasing/adding mappings on the fly
        // Don't do that now.

    private:
        // This is the set of flags we modify
        F                    flags;

        // Only enums found in this map are recognized
        static flag_map_type flag_map;
};

// declare the static datamember
template <typename E, typename F, bool b>
typename flagset_type<E,F,b>::flag_map_type flagset_type<E,F,b>::flag_map = typename flagset_type<E,F,b>::flag_map_type();


// And define how a flagset_type gets written on a std::ostream
template <typename E, typename F,bool b>
std::ostream& operator<<(std::ostream& os, const flagset_type<E,F,b>& fst) {
    bool                                                         comma( false );
    const typename flagset_type<E,F,b>::flag_map_type&           fm( flagset_type<E,F,b>::get_flag_map() );
    typename flagset_type<E,F,b>::flag_map_type::const_iterator  curflag;

    os << "<";
    // Loop over all defined flags and see if they're set
    for( curflag=fm.begin(); fst.empty()==false && curflag!=fm.end(); ++curflag ) {
        if( fst.is_set(curflag->first) ) {
            (void)(comma && (os << ','));
            os << curflag->second.__nm;
            comma=true;
        }
    }
    if( fst.empty() )
        os << "NO FLAGS SET";
    os << ">";
    return os;
}

#endif
