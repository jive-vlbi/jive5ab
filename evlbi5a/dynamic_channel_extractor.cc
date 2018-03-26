// generate C code that will compile to a channel extractor
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
#include <dynamic_channel_extractor.h>
#include <map>
#include <set>
#include <algorithm>

#include <ctype.h>
#include <stdio.h>  // for ::sscanf()
#include <stdint.h> // for uint8_t

#include <stringutil.h>

// If your project does not have 'ezassert.h' (see 
// dynamic_channel_extractor.h) - delete the following
// line and edit the DCEASSERT() macros below
// to fit your needs. The second argument to the macro
// is something that should be inserted to a std::ostream,
// eg std::cerr.
DEFINE_EZEXCEPT(dce_error)

// Use as:
//    DCEASSERT( <assertion> )
//    DCEASSERT(::malloc(num)!=0 )
//    or
//    DCEASSERT2( <assertion>, <string/stream> );
//    DCEASSERT2(::malloc(num)!=0, "failed to allocate " << num << " bytes");
#define DCEASSERT(a)     EZASSERT(a, dce_error)
#define DCEASSERT2(a, e) EZASSERT2(a, dce_error, EZINFO(e))

using namespace std;

// take a string of the format
//   int>[int,int,int],[int,int,int]
//   <#ofbitsperinputword> '>' <CSV-list-of-bitlists>
//   <bitlist> = [int,int, ...] the bit numbers from the
//                              source word that make up
//                              that section
typedef map<unsigned int, string>   linemap_type;


extractorconfig_type::extractorconfig_type(unsigned int bitsperinput,
                                           const channellist_type& chlist):
    bitsperinputword( bitsperinput ),
    bitsperchannel( (chlist.size()>0)?(chlist[0].size()):(unsigned int)-1 ),
    channels( chlist )
{
    channellist_type::const_iterator curchan;

    // Do some basic assertions
    DCEASSERT(bitsperinputword <= 64);
    DCEASSERT(chlist.size()>0);

    curchan        = chlist.begin();
    while( curchan!=chlist.end() ) {
        DCEASSERT(curchan->size() == bitsperchannel );
        curchan++;
    }  
}

ostream& operator<<(ostream& os, const extractorconfig_type& config ) {
    channellist_type::const_iterator  curchan = config.channels.begin();
    os << config.bitsperinputword << " > ";

    while( curchan!=config.channels.end() ) {
        channelbitlist_type::const_iterator curbit = curchan->begin();
        os << "[";
        while( curbit!=curchan->end() ) {
            os << *curbit;
            if( ++curbit!=curchan->end() )
                os << ',';
        }
        os << "]";
        curchan++;
    }
    return os;
}

extractorconfig_type parse_dynamic_channel_extractor(const string& configstr ) {
    typedef vector<pair<string::iterator, string::iterator> > channeldef_type;
    size_t                          bitsperchannel = 0;
    string                          config( configstr );
    unsigned int                    inputincrement_bits;
    openclose_type                  channeldelim = {'[', ']' };
    vector<string>                  parts;
    channeldef_type                 channeldefs;
    channellist_type                channels;
    channeldef_type::const_iterator curchan;

    // Remove all whitespace
    config.erase(std::remove_if(config.begin(), config.end(), ::isspace), config.end());

    DCEASSERT( config.empty()==false );
    DCEASSERT2( (parts= ::split(config, '>')).size()==2,
                  "syntax error in splitstring, no/too many '>' found" );

    // Analyze the first number: the number of inputbits 
    DCEASSERT2( parts[0].empty()==false && ::sscanf(parts[0].c_str(), "%u", &inputincrement_bits)==1,
                  "'" << parts[0] << "' appears not to be a number" );
    DCEASSERT2( inputincrement_bits<=64,
                  inputincrement_bits << " is too large. maximum supported is 64" );
    DCEASSERT2( (channeldefs = ::find_enclosed(parts[1].begin(), parts[1].end(), channeldelim)).size()>0,
                  "you did not configure any channels" );
    for( curchan=channeldefs.begin(); curchan!=channeldefs.end(); curchan++ ) {
        string              channeldef = string(curchan->first, curchan->second);
        channelbitlist_type channelbits;

        DCEASSERT2( channeldef.empty()==false, "the channeldefinition is empty?!" );
        DCEASSERT2( (channelbits = ::parseUIntRange(channeldef, ',')).size()>0,
                    "the channeldefinition '" << channeldef << "' is empty?" );

        // Make sure each channel has the same number of bits set
        if( curchan==channeldefs.begin() )
            bitsperchannel = channelbits.size();
        else
            DCEASSERT2( channelbits.size()==bitsperchannel,
                        "the number of bits per channel varies. channel '" << channeldef[0] << "' is not " << bitsperchannel );
        // Verify that the bits addressed do not address outside of the
        // inputword
        for(size_t bit=0; bit<channelbits.size(); bit++)
            DCEASSERT2( channelbits[bit]<inputincrement_bits,
                        "bit #" << bit << " of channel " << channels.size()+1 << " is out of range, max " << inputincrement_bits);
        // Now it's safe to add the channeldefinition to our list of
        // channels
        channels.push_back( channelbits );
    }
    DCEASSERT2( channels.size()>0, "no channels defined?!" );
    return extractorconfig_type(inputincrement_bits, channels);
}


struct bitkey_type {
    unsigned int    srcbyte;
    unsigned int    dstbyte;
    int             shift;
    mutable uint8_t mask;

    bitkey_type(unsigned int sbyte, unsigned int dbyte, int s, uint8_t m):
        srcbyte( sbyte ), dstbyte( dbyte ), shift( s ), mask( m )
    {}
};
ostream& operator<<(ostream& os, const bitkey_type& bk) {
    os << "[srcbyte:" << bk.srcbyte << " dstbyte:" << bk.dstbyte
       << " shift:" << bk.shift << " mask:0x" << hex << (unsigned int)bk.mask << dec
       << "]";
    return os;
}

struct ltbitkey {
    bool operator()(const bitkey_type& l, const bitkey_type& r) const {
        if( l.srcbyte!=r.srcbyte )
            return l.srcbyte<r.srcbyte;
        if( l.dstbyte!=r.dstbyte )
            return l.dstbyte<r.dstbyte;
        return l.shift<r.shift;
    }
};

typedef std::set<bitkey_type,ltbitkey>  bitset_type;


struct channelstate_type {

    channelstate_type(unsigned int n):
        idx( n )
    {}

    // return the complete code for updating the unsigned char
    // array named by 'output'; it will produce code like:
    //   "<out>[n] = (<in>[z] & foo )>>bar) ... ;"
    // It is assumed the caller increments the out/in pointers
    string  getlines(const string& out, const string& in) const {
        string                      rv;
        linemap_type                linemap;
        bitset_type::const_iterator ptr;
        
        DCEASSERT2(this->complete(), "Cannot generate code for incomplete solution");

        for( ptr=bitset.begin(); ptr!=bitset.end(); ptr++ ) {
            linemap_type::iterator lineptr;

            // Do we already have the "lead" for the current destination
            // byte?
            if( (lineptr=linemap.find(ptr->dstbyte))==linemap.end() ) {
                ostringstream                      line;
                pair<linemap_type::iterator, bool> insres;
                // nope, create it
                line << "\t\t" << out << "[" << ptr->dstbyte << "] = \n";
                DCEASSERT2( (insres=linemap.insert(
                                                    make_pair(ptr->dstbyte, line.str())
                                                    )).second,
                            "failed to insert entry for dest byte #" << ptr->dstbyte << " Ch" << idx );
                // check!
                lineptr = insres.first;
            } else {
                lineptr->second += " | \n";
            }

            // Generate code for the current bit:
            //   move 'bits[bitidx]'
            //   from 
            //      [srcbyte][srcbit]   (==bits[bitidx])
            //   to
            //      [dstbyte][dstbit]   (==bitidx)
            string         isolate, shift;
            ostringstream  s1, s2;

            s1 << "(" << in << "[" << ptr->srcbyte << "] & " << hex << "0x" << (unsigned int)ptr->mask << dec << ")";
            isolate = s1.str();

            if( ptr->shift<0 )
                s2 << " << " << -ptr->shift;
            else if( ptr->shift>0 )
                s2 << " >> " << ptr->shift;
            shift = s2.str();

            if( shift.empty()==false )
                isolate = string("(")+isolate + shift + ")";
            isolate = string("\t\t\t")+isolate;
            lineptr->second += isolate;
        }
        // Great, we have processed all bits!
        // All that's left is to end each line with a ';'
        // and add them all up to produce a return value
        for(linemap_type::iterator lptr=linemap.begin();
            lptr!=linemap.end(); lptr++ )
                rv += (lptr->second + " ;\n");
        return rv;
    }

    bool complete( void ) const {
        return (bits.size()>0 && bits.size()%8==0);
    }

    unsigned int outputincrement( void ) const {
        DCEASSERT2(this->complete(), "cannot call this on a partial channelstate!");
        return bits.size()/8;
    }

    void addbit( unsigned int bitnum ) {
        const unsigned int     bitidx = bits.size();
        const unsigned int     dstbit( bitidx%8 ), dstbyte( bitidx/8 );
        const unsigned int     srcbit( bitnum%8 ), srcbyte( bitnum/8 );
        const bitkey_type      key(srcbyte, dstbyte, (int)(srcbit-dstbit), (uint8_t)(0x1<<srcbit));
        bitset_type::iterator  ptr = bitset.find( key );
      
        // find an existing entry for the key (add if none yet) 
        if( ptr==bitset.end() ) {
            pair<bitset_type::iterator, bool>  insres = bitset.insert( key );
            DCEASSERT2( insres.second, "failed to insert entry into bitset: bit#" << bitnum << " [@" << bitidx <<"]" );
            ptr = insres.first;
        }
        // now add the current bit to the mask
        ptr->mask |= key.mask;

        bits.push_back(bitnum);
    }

    private:
        // the channel's number
        bitset_type         bitset;
        unsigned int        idx;
        channelbitlist_type bits;
};

typedef vector<channelstate_type> extractorstate_type;

// Generate 
//    void <functionname>(void* src, size_t len, void* d0, ... dN)
string generate_dynamic_channel_extractor(const extractorconfig_type& config, const string& functionname) {
    char                           datebuf[64];
    time_t                         gentime_ux; 
    struct tm                      gentime_gm;
    const string                   inputname( "input" );
    unsigned int                   bitoffset;
    unsigned int                   inputincrement_bytes;
    unsigned int                   outputincrement_bytes;
    ostringstream                  code;
    vector<string>                 parts;
    vector<string>                 channeldefs;
    vector<string>                 outputname;
    const unsigned int&            inputincrement_bits( config.bitsperinputword );
    const channellist_type&        channels( config.channels );
    extractorstate_type            extractorstate;
    vector<string>::const_iterator curchan;

    DCEASSERT2( functionname.empty()==false, "no functionname given!" );

    // Create the extractorstate - the extractorstate c'tor has already
    // asserted a number of things for us
    for(size_t chidx=0; chidx<channels.size(); chidx++)
        extractorstate.push_back( channelstate_type(chidx) );

    // Keep on adding the channelbits to the channelstate until we've hit a
    // "complete" solution, ie the output contains an integral multiple of 
    // 8-bit bytes. Note: it is asserted that all channels have the same
    // amount of bits per channel so it is sufficient to test only one of
    // them for 'completeness'. 
    // Also it is asserted that there *are* channels so channel[0] *is*
    // adressable
    bitoffset = 0;
    while( extractorstate[0].complete()==false || (bitoffset>0 && bitoffset%8) ) {
        for(size_t chidx=0; chidx<extractorstate.size(); chidx++)
            for(channelbitlist_type::const_iterator bitptr = channels[chidx].begin();
                bitptr!=channels[chidx].end(); bitptr++)
                    extractorstate[chidx].addbit(*bitptr + bitoffset);
        bitoffset += inputincrement_bits;
    }
    // Hopla. That was that.
    inputincrement_bytes  = bitoffset / 8;
    outputincrement_bytes = extractorstate[0].outputincrement();

    // date + time of generation
    ::time( &gentime_ux );
    ::gmtime_r( &gentime_ux, &gentime_gm );
    ::strftime(datebuf, sizeof(datebuf), "%A %d %B %Y %H:%M:%S", &gentime_gm);

    // For each channel, generate an output pointer
    for(size_t i=0; i<extractorstate.size(); i++) {
        ostringstream  tmp;
        tmp << "d" << i;
        outputname.push_back( tmp.str() );
    }


    // Produce the code!
    // File header
    code << "/* this file was generated by dynamic channel extractor v0.2" << endl
         << " * by jive5ab on " << datebuf << endl
         << " *" << endl
         << " * config:" << endl
         << " * " << config << endl
         << " */" << endl
         << "#include <stdint.h>" << endl
         << "#include <sys/types.h>" << endl
         << "#include <stdio.h>" << endl
         << endl;

    // Function prototype - including "{"
    code << "void " << functionname << "(void* src, size_t len";

    for(size_t i=0; i<extractorstate.size(); i++)
        code << ", void* dst" << i;
    code << ") {" << endl;
   
    // Function body
    //   * local variable declarations - we must cast void* to uint8_t*
    code << "\tuint8_t*\t\t" << inputname << " = (uint8_t*)src;" << endl; // begin
    code << "\tuint8_t* const\tendptr = (uint8_t* const)(" << inputname << " + len);" << endl; // end
    for(size_t i=0; i<extractorstate.size(); i++)
        code << "\tuint8_t*\t\t   " << outputname[i] << " = (uint8_t*)dst" << i << ";" << endl;
    code << endl;

    // Start loop ...
    code << "\twhile( (" << inputname << " + " << inputincrement_bytes << ") <= endptr ) {" << endl;

    for(size_t i=0; i<extractorstate.size(); i++)
        code << extractorstate[i].getlines(outputname[i], inputname);
    code << "\t\t" << inputname << " += " << inputincrement_bytes << " ;" << endl;
    for(size_t i=0; i<extractorstate.size(); i++)
        code << "\t\t" << outputname[i] << " += " << outputincrement_bytes << ";" << endl;

    code << "\t}" << endl;

    // end of the function body
    code << "}" << endl;

    std::cout << code.str() << endl;
    return code.str();
}
