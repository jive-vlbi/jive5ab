// drop selected bitstreams from VLBI data
// Copyright (C) 2009-2010 Bob Eldering (mods by Harro Verkouter)
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
// Author:  Bob Eldering - eldering@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <trackmask.h>

#include <stdlib.h> // ::abs()
#include <stdio.h>  // ::popen(), fileno()
#include <math.h>   // ::pow()
#include <errno.h>  // ...


// for sort()
#include <algorithm> // sort()
#include <iterator>  //  ...
#include <sstream>
#include <fstream>
#include <set>

// Set linker command + extension of shared library based on O/S
#ifdef __linux__
    #define LOPT  " -shared"
    #define SOEXT ".so"
#elif __APPLE__ & __MACH__
    #define LOPT  " -dynamiclib"
    #define SOEXT ".dylib"
#else
    #error Undefined system
#endif

// For the "Bits2Build" (32 or 64bit version)
#define BOPT " -m"<<B2B

using namespace std;

//
//  First, let's get the static stuff out of the way
//

// our basic unit of compression
const int       nbit            = (sizeof(data_type)*8);
const string    data_type_name  = "unsigned long long int";
const data_type trackmask_empty = (data_type)0;
const data_type trackmask_full  = ~trackmask_empty;

void*                      compressor_type::handle = 0;
data_type                  compressor_type::lastmask = trackmask_empty;
unsigned int               compressor_type::blocksize = 0;
compressor_type::fptr_type compressor_type::compress_fn   = &compressor_type::do_nothing;
compressor_type::fptr_type compressor_type::decompress_fn = &compressor_type::do_nothing;

// local prototype - this function only lives inside this compilation unit. 
// solve a "problem" (1st argument), giving a set of possibilities in the
// 2nd argument, ordered by "quality". Quality is a measure of how many bits
// were compressed into how many words in how many steps.
void partial_solve(const solution_type& /* problem */,
                   solutions_t& /* accumulator for possible solutions */,
                   const unsigned int /* max recursion depth */,
                   unsigned int depth = 0 /* current recursion depth*/ );


//
// At last, some code!
//
step_type::step_type():
    shift( 0 ), inc_dst( false ), dec_src( false ),
    mask_from( 0  ), mask_to( 0 ),
    bits_moved( 0 )
{}

// construct a step 
step_type::step_type(data_type mask_f, data_type mask_t, int s):
    shift( s ), inc_dst( false ), dec_src( false ),
    mask_from( mask_f ), mask_to( mask_t ),
    bits_moved( count_bits(mask_from) )
{}

// for solution::add_step()
step_type::step_type(const step_type& other, bool src, bool dst):
    shift( other.shift ), inc_dst( dst ), dec_src( src ),
    mask_from( other.mask_from ), mask_to( other.mask_to ),
    bits_moved( other.bits_moved )
{}

step_type::operator bool() const {
    return (mask_from!=trackmask_empty);
}

std::string step_type::compress_code( void ) const {
    ostringstream s;
    s << "C[" << hex_t(mask_from) << " ";
    if( shift )
        s << "bs" << ((shift<0)?('r'):('l')) << " " << setw(2) << ::abs(shift) << " ";
    s << "=> " << hex_t(mask_to) << " ";
    if( inc_dst )
        s << "dst++ ";
    if( dec_src )
        s << "src-- ";
    s << "]";
    return s.str();
}
std::string step_type::decompress_code( void ) const {
    ostringstream s;
    s << "D[" << hex_t(mask_to) << " ";
    if( shift )
        s << "bs" << ((shift>0)?('r'):('l')) << " " << setw(2) << ::abs(shift) << " ";
    s << "=> " << hex_t(mask_to) << " ";
    if( inc_dst )
        s << "dst++ ";
    if( dec_src )
        s << "src-- ";
    s << "]";
    return s.str();
}

string step_type::compress_code(const variable_type& source, const variable_type& dest) const {
    ostringstream  s;
    variable_type::action_type  sourceaction( dec_src?variable_type::post_dec_addr:variable_type::noop );
    variable_type::action_type  destaction( inc_dst?variable_type::post_inc_addr:variable_type::noop );

    s << hex_t(mask_from) << "ull";
    local_variable m_from(s.str());

    // The general idea is:  *dest++ |= ((*source & mask_from) [<<|>> <shift>])
    s.str( string() );
    s << dest.ref(destaction) << " |= ";
    if( shift )
       s<< "((";
    s << source.ref(sourceaction) << "&" << m_from;
    if( shift )
        s << ") " << ((shift<0)?(">> "):("<< ")) << ::abs(shift) << ")";
    s << ";";
    return s.str();
}

string step_type::decompress_code(const variable_type& source, const variable_type& dest) const {
    ostringstream  s;
    // decompression goes the other way round;
    // if the compression decreased the sourcepointer, we decrease the destpointer.
    // conversely, if the compression increased the destptr, we increase the sourcepointer
    variable_type::action_type  sourceaction( inc_dst?variable_type::post_inc_addr:variable_type::noop );
    variable_type::action_type  destaction( dec_src?variable_type::post_dec_addr:variable_type::noop );

    s << hex_t(mask_to) << "ull";
    local_variable m_to(s.str());

    // The general idea is:  *out++ |= ((in & mask_to) [>>|<< <shift>])
    s.str( string() );
    s << dest.ref(destaction) << " |= ";
    if( shift )
       s<< "((";
    s << source.ref(sourceaction) << "&" << m_to;
    if( shift )
        s << ") " << ((shift<0)?("<< "):(">> ")) << ::abs(shift) << ")";
    s << ";";
    // Also clear the mask_to bits in "in": in++ &= ~mask_to
    //s << source.ref(sourceaction) << " &= ~" << m_to << ";";
    // well, officially, we should do it like this. for efficiency reasons
    // we let the upperlevel code handle this
    return s.str();
}

ostream& operator<<(ostream& os, const step_type& s) {
    return os << s.compress_code();
}

// a solution is a series of steps + some bookkeeping for keeping track
// of its quality
solution_type::solution_type():
    q_value( 0 ), full_cycle( true ),
    mask_in( trackmask_empty ), mask_out( trackmask_empty ), trackmask( trackmask_empty ),
    n_dstinc( 0 ), n_srcdec( 0 ), n_bits_moved( 0 )
{}

solution_type::solution_type(data_type bitstokeep) {
    // Delegate to other c'tor by creative usage of the
    // placement operator new().
    new (this) solution_type(bitstokeep, bitstokeep);
}

solution_type::solution_type(data_type bitstomove, data_type bitstokeep):
    q_value( 0 ), full_cycle( bitstomove==bitstokeep ),
    mask_in( bitstomove ), mask_out( bitstokeep ), trackmask( bitstokeep ),
    n_dstinc( 0 ), n_srcdec( 0 ), n_bits_moved( 0 )
{}

bool solution_type::complete( void ) const {
    return (mask_in==trackmask_empty && (full_cycle?(mask_out==trackmask_full):true));
}

unsigned int solution_type::outputsize( unsigned int inputsize ) const {
    unsigned int dutycycle( cycle() );

    // if our duty-cycle == 0, we're doomed!
    ASSERT_COND( complete() && dutycycle>0 );

    unsigned int               opsize = (inputsize/dutycycle) * n_dstinc;
    unsigned int               remain = (inputsize%dutycycle);
    steps_type::const_iterator curstep;

    // keep on looping until we've processed the remaining words.
    // Note: we do not have to check against steps.end() since ... if we
    // would have reached steps.end() it would mean we would've completed a
    // full cycle and hence it would already have been counted. Unless our
    // internal state is buggered we should now only have to process a
    // partial cycle.
    for(curstep=steps.begin(); remain; curstep++) {
        // the following condition is sufficient since they cannot both be
        // true at the same time because it is EXACTLY that step which is
        // last that has both inc_dst && dec_src true
        // [well, at least for solutions that do a "full_cycle" - partial
        // solutions may or may not have this but you wouldn't want to use
        // those partial solutions to compress a block of data anyway. If
        // you would, however, you're on your own.
        if( curstep->inc_dst || curstep->dec_src )
            remain--;
        // if this step increments the dst we have to count that into the
        // outputsize
        if( curstep->inc_dst )
            opsize++;
    }
    if( curstep!=steps.begin() &&
        (curstep-1)->inc_dst==false )
        opsize++;
    return opsize;
}

// given a desired outputsize (compressed size), return how many
// uncompressed words you'd have to feed the compressor to achieve that
unsigned int solution_type::inputsize( unsigned int outputsize ) const {
    unsigned int dutycycle( cycle() );

    // if our duty-cycle == 0, we're doomed!
    ASSERT_COND( dutycycle>0 );
    ASSERT_COND( n_dstinc>0 );

    // each dutycycle amount of inputwords yields n_dstinc of ouputwords so
    // we can easily figure out how many times we can completely do this and
    // how many outputwords remain
    unsigned int               ipsize = (outputsize/n_dstinc) * dutycycle;
    unsigned int               remain = (outputsize%n_dstinc);
    steps_type::const_iterator curstep;

    // keep on looping until we've output all the remaining words.
    // Note: we do not have to check against steps.end() since ... if we
    // would have reached steps.end() it would mean we would've completed a
    // full cycle and hence it would already have been counted. Unless our
    // internal state is buggered we should now only have to process a
    // partial cycle.
    for(curstep=steps.begin(); remain; curstep++) {
        // the following condition is sufficient since they cannot both be
        // true at the same time because it is EXACTLY that step which is
        // last that has both inc_dst && dec_src true
        // [well, at least for solutions that do a "full_cycle" - partial
        // solutions may or may not have this but you wouldn't want to use
        // those partial solutions to compress a block of data anyway. If
        // you would, however, you're on your own.]
        if( curstep->dec_src )
            ipsize++;
        if( curstep->inc_dst )
            remain--;
    }
    if( curstep!=steps.begin() &&
        (curstep-1)->inc_dst==false )
        ipsize++;
    return ipsize;
}

string solution_type::summary( void ) const {
    ostringstream o;
    o << "/nBit:" << n_bits_moved << "/nStep:" << steps.size() 
      << "/Q:" << quality()
      << "/" << n_srcdec << " src--/" << n_dstinc << " dst++/";
    return o.str();
}
data_type solution_type::mask( void ) const {
    return trackmask;
}
unsigned int solution_type::cycle( void ) const {
    return n_srcdec + n_dstinc;
}
unsigned int solution_type::compressed_cycle( void ) const {
    return n_dstinc;
}

// compute the quality of this solution.
// higher number is better quality
int solution_type::compute_quality( void ) const {
    if( steps.size()==0 || n_bits_moved==0 )
        return 0;
    // complete solutions are always very good to have.
    // they are degraded by their compression ratio:
    // number-of-words-processed/number-of-output-words.
    // the maximum compressionratio is (nbit-1).
    // maximum amount of bits/step that is possible is (nbit-1)
    // (since we require that at least 1 bit is set in the bits-to-keep)
    // we compute both the compressionratio and bits/step as doubles and
    // multiply by 1000 to get at three decimal places of accuracy)
    int     q( complete()?(INT_MAX- (2000 * nbit)):0 );
    double  t;

    // compressionratio. if n_srcdec+n_dstinc==0 we're still
    // doing stuff in the same word. in this case we use
    // the bitratio between out/in as compressionmeasure
    // in order to mark the solutions which give high bitcompression
    // also a viable option
    const unsigned int words_processed(n_srcdec + n_dstinc);
    if( words_processed==0 )
        t  = 1000.0 * ((double)count_bits(mask_out)/(double)count_bits(mask_in));
    else
        t  = 1000.0 * ((double)n_dstinc/(double)words_processed);
    q += (int)t;

    // and average bits/step
    t  = 1000.0 * ((double)n_bits_moved/steps.size());
    q += (int)t;

    return q;
}

// Sometimes multiple steps can be organized into one
// check if there exists a step which has neither src_dec nor dst_inc set.
// Only those steps are eligible for combining
struct is_eligible {
    bool operator()(const step_type& s) const {
        return !(s.dec_src || s.inc_dst);
    }
};
#define FIND_ELIGIBLE(f, l) \
    std::find_if(f, l, is_eligible())

typedef std::pair<steps_type::const_iterator, steps_type::const_iterator> eligible_range_type;
typedef std::list<eligible_range_type> eligible_ranges_type;

eligible_ranges_type find_eligible_ranges(const steps_type& steps) {
    is_eligible                eligible;
    eligible_ranges_type       rv;
    steps_type::const_iterator p = FIND_ELIGIBLE(steps.begin(), steps.end());

    while( p!=steps.end() ) {
        steps_type::const_iterator q = p;

        while( ++q!=steps.end() && eligible(*q) );
        // q now points at the last step that should be *included* since it
        // was the first step that was not eligible, however, it still
        // operates on the same word since all dec_src/inc_dst are POST
        // operations. The range we should be returning should have the
        // second pointer pointing 1 *past* the last step to include
        if( q!=steps.end() )
            q++;
        // if q STILL !=steps.end() we've found a usable eligible range
        // since if the eligible range is at the end, the next step - if any
        // ever - may (or may not) also be included in the eligible range. since
        // we cannot tell we do not count it as eligible range *yet*
        if( q!=steps.end() )
            rv.push_back( eligible_range_type(p, q) );
        // search for the start of the next range
        p = FIND_ELIGIBLE(q, steps.end());
    }
    return rv;
}


void solution_type::sanitize( void ) {
    typedef back_insert_iterator<steps_type> step_append_type;

    steps_type                           newsteps;
    step_append_type                     appender(newsteps);
    eligible_ranges_type                 ranges = find_eligible_ranges(steps);
    steps_type::const_iterator           lastptr = steps.begin();
    steps_type::const_iterator           endptr  = steps.end();
    eligible_ranges_type::const_iterator range;

    if( ranges.size()==0 )
        return;

    for(range=ranges.begin(); range!=ranges.end(); range++) {
        data_type                  from = 0, to = 0;
        solutions_t                solutions;
        steps_type::const_iterator p;
        // before we do anything copy all steps from lasptr up-to the
        // beginning of this range into newsteps
        copy(lastptr, range->first, appender);

        // compute which bits were moved
        for(p=range->first; p!=range->second; p++)
            from |= p->mask_from, to |= p->mask_to;

        // solve for moving the bits in FROM into the positions of ~TO
        // (since TO contains the positions where we moved the bits so far.
        // For solving we clear those locations (and mark the others as
        // "taken"), both of which goals are reached with a bitwise-not.
        // Now the solve() algorithm will try and see if it can move the
        // bits mentioned in FROM into the "holes" in "~TO" more efficiently
        partial_solve(solution_type(from, ~to), solutions, 3);

        // Pick the first solution (they're ordered by quality), if any
        if( solutions.size() ) {
            solution_type              sout( *solutions.begin() );
            steps_type::const_iterator origlast;
            
            // before we copy this partial solution into our series of steps
            // we must copy over the inc_dst/dec_src properties of the last
            // step in the original sequence
            origlast = range->second;
            advance(origlast, -1);
            sout.rbegin()->inc_dst = origlast->inc_dst;
            sout.rbegin()->dec_src = origlast->dec_src;
            copy(sout.begin(), sout.end(), appender);
        } else 
            copy(range->first, range->second, appender);
        lastptr = range->second;
    }
    // DO!NOT!FORGET!TO!COPY!REMAINDER!OF!STEPS!IF!ANY!KRYST!
    // (you figure out how I knew this was important!)
    copy(lastptr, endptr, appender);

    if( newsteps.size() < steps.size() ) {
        steps   = newsteps;
        // we haven't altered the number of src_dec/dst_inc
        // only the steps
        q_value = compute_quality();
    }
}

// We say that two solutions are logically equivalent if the source and
// (final) destination are equal after the same amount of steps, forgetting
// about source-decs and dst-incs. 
// How could we tell them apart? And frankly: do we need to be able to
// tell them apart? If, for all practical purposes, they do the same ...
// what do we care?
// [We do assume each step takes the same amount of time]
//
// I have no proof but ran a couple of iterations and it seems that
// the condition where (mask_in, mask_out, n_bitsmoved, steps.size()) all
// match with those of "other" whilst at least one of (n_srcdec, n_dstinc)
// does NOT match up with "other"'s does not seem to arise. The quality
// associated with "this" and "other" may yet be different but that's a
// different test.
bool solution_type::operator==(const solution_type& other) const {
    return (mask_in==other.mask_in &&
            mask_out==other.mask_out &&
            n_bits_moved==other.n_bits_moved &&
            steps.size()==other.steps.size());
}

// add the indicated step to this solution and
// compute its effect
void solution_type::add_step( const step_type& s ) {
    // only allow adding steps when there are still
    // bits left in our input to move.
    // Also test that all the bits adressed by the step
    // are available in our input and that the holes
    // in the output are also available
    ASSERT2_COND( complete()==false,
                  SCINFO("This solution is already complete. Thou shalt not add further steps.") );
    ASSERT2_COND( (mask_in&s.mask_from)==s.mask_from,
                  SCINFO("Not all sourcebits mentioned in 'from' are available in 'in':" << endl <<
                         "mask_in  : " << bin_t(mask_in) << endl <<
                         "step: " << s << endl) );
    ASSERT2_COND( ((~mask_out)&s.mask_to)==s.mask_to,
                  SCINFO("Not all outputbits mentioned in 'to' are free in 'out':" << endl <<
                         "mask_out : " << bin_t(mask_out) << endl <<
                         "mask_to  : " << bin_t(s.mask_to) << endl));

    // Compute the effect of adding step s:
    //  1. remove the bits from our current "input"
    mask_in  &= ~s.mask_from;
    //  2. add them to the current "output"
    mask_out |=  s.mask_to;

    // depending on whether we emptied another source word or
    // filled another dest word we inc/dec the appropriated address
    const bool dst_full       = (mask_out==trackmask_full);
    const bool src_empty      = (mask_in==trackmask_empty);
    const bool reset_mask_in  = (src_empty && !complete());
    const bool reset_mask_out = (dst_full && !complete());


    // dst_full && src_empty => complete
    // !complete =>
    //    possible [source_dec OR dest_inc] + restart with trackmask
    //    (never at the same time, otherwise complete)
    // note: xor would not do the trick (only logically) but we must
    //       exactly know WHICH of the two (src or dest) needs to be
    //       incremented and reset
    if( dst_full )
        n_dstinc++;
    if( src_empty )
        n_srcdec++;
    if( reset_mask_in )
        mask_in  = trackmask;
    if( reset_mask_out )
        mask_out = trackmask;

    // add the amount of bits moved in this step
    n_bits_moved += s.bits_moved;

    // add the step
    steps.push_back( step_type(s, src_empty, dst_full) );

    // and re-compute our quality
    q_value = compute_quality();

    return;
}

solution_type solution_type::operator+(const step_type& s) const {
    solution_type r( *this );
    r.add_step( s );
    return r;
}

ostream& operator<<(ostream& os, const solution_type& s) {
    steps_type::const_iterator curstep;

    os << "K: " << hex_t(s.trackmask) << endl
       << "   " << s.summary() << endl;
    if( !s.complete() ) {
        os << " In :" << hex_t(s.mask_in) << endl
           << " Out:" << hex_t(s.mask_out) << endl;
    }
    for(curstep = s.steps.begin(); curstep!=s.steps.end(); curstep++)
        cout << curstep->compress_code() << endl;
    return os;
}

bool operator<(const solution_type& l, const solution_type& r) {
    if( l==r )
        return false;
    return l.quality()>r.quality();
}

struct solution_sort {
    bool operator()(const solution_type& l, const solution_type& r) const {
        return l.quality()>r.quality();
    }
};

struct solution_equal {
    bool operator()(const solution_type&l, const solution_type& r) const {
        return (l==r);
    }
};

// return a vector of possible solutions that move a non-zero amount of bits
// this is a mighty important function as we should supply only the 
// amount of possibilities that make sense given the circumstances.
// We've added edge-detection criteria.
// If, in the source or dest there are 2 or less edges (transitions from
// 0->1 and 1->0) we only supply the solution which moves the maximum amount
// of bits - eliminating very deep searches for the trivial cases.
// 2 or less edges means that there are large "blocks" of 1s and 0s
// available (source) or free (dest) and as such there's very little
// point in looking how to fill these smartly.
// Dumb-assed greedy filling is, in these cases, good enough.

struct step_ordering {
    // implement strict weak ordering for steps: if they move more bits
    // they are gooder and hence we put them at the front
    bool operator()(const step_type& l, const step_type& r) const {
        return l.bits_moved>r.bits_moved;
    }
};
typedef multiset<step_type, step_ordering> possible_type;

solutions_t possibilities(const solution_type& in) {
    // compile a list of steps that we could append to the current solution
    int              shift;
    data_type        tmp, ssrc;
    possible_type    steps;
    const data_type  source( in.source() );
    const data_type  dest( in.dest() );
    const data_type  notdest( ~dest );
//    const bool       onlymax( count_edges(dest)==count_edges(source) );
//    const bool       onlymax( count_edges(dest)<=2 );
//    const bool       onlymax( true );
//    const bool       onlymax( true );
    const bool       onlymax( true );
//    const bool       onlymax( count_edges(dest)==1 && count_edges(source)==1 );
//    const bool       onlyfirst( count_edges(dest)==1 && count_edges(source)==1 );
//    const bool       onlyfirst( false );
    const bool       onlyfirst( true );

    // use loop unrolling for both shift directions so the compiler 
    // can (hopefully) optimize a bit. take care to do the zero-shift
    // only once

    // the negative shifts -1 -> (-nbit+1).
    // that is: until we have shifted out all bits in source (this
    // shifts shift in '0's at the other end so if we have no bits
    // left in source there's no point in continuing.
    for(ssrc=(source>>1), shift=-1; ssrc; ssrc>>=1, shift--)
        // compute if this shift *can* move some bits
        if( (tmp=(ssrc&notdest)) )
            // yes. add to the possibilities. note: shift<0, hence we must
            // use << (-shift) ... d'oh.
            steps.insert( step_type(tmp<<(-shift), tmp, shift) );
    // id. for the positive shifts. note: this one processes the 0 shift.
    for(ssrc=source, shift=0; ssrc; ssrc<<=1, shift++)
        // compute if this shift *can* move some bits
        if( (tmp=(ssrc&notdest)) )
            // yes. add to the possibilities
            steps.insert( step_type(tmp>>shift, tmp, shift) );
    // Prepare the list of possible solutions, taking into
    // account if we only need to take the one that moves
    // most bits
    solutions_t                   r;
    possible_type::const_iterator ptr  = steps.begin();
    possible_type::const_iterator eptr = steps.end();

    // if we were looking for the maximum amount of shifts,
    // consider only those solutions that move that amount of 
    // bits
    if( onlymax ) {
        eptr = steps.equal_range(*steps.begin()).second;
        if( eptr!=steps.end() )
            eptr = steps.equal_range(*eptr).second;
    }
    if( onlyfirst && steps.size() ) {
        eptr = steps.begin();
        eptr++;
    }
    // Only steps that actually move bits are included so we don't have
    // to look at each step to check IF it does something.
    // Append the new solution, based on our input solution plus the
    // current step.
    while( ptr!=eptr )
        r.insert( in+*ptr++ );
    return r;
}


struct is_complete {
    bool operator()(const solution_type& s) const {
        return s.complete();
    }
};

#define FIND_COMPLETE(l) \
    std::find_if(l.begin(), l.end(), is_complete())

void partial_solve(const solution_type& solution_in, solutions_t& v,
                   const unsigned int maxdepth, unsigned int depth) {
    // do we need to stop recursing?
    if( solution_in.complete() || depth>maxdepth ) {
        // append the sanitized solution to the accumulator
        solution_type  tmp( solution_in );
        tmp.sanitize();
        v.insert( tmp );
        return;
    }
    // Get the possible continuations for the given inputsolution
    solutions_t                   solutions = possibilities(solution_in);

    if( solutions.empty() )
        return;
    // and solve those
    for(solutions_t::const_iterator cursol=solutions.begin(); cursol!=solutions.end(); cursol++)
        partial_solve(*cursol, v, maxdepth, depth+1);
    return;
}

solution_type solve(data_type trackmask) {
    return solve(solution_type(trackmask), 100);
}

solution_type solve(const solution_type& solution_in, unsigned int niter) {
    solutions_t           p;
    solutions_t::iterator ptr;  // defines pointer to last solution to search

    // seed our list of solutions with the one we want to solve
    p.insert( solution_in );
    // now start looping until we find a complete solution.
    // if there are no solutions to check, we give up :(
    while( ((ptr=FIND_COMPLETE(p))==p.end()) && p.size() && niter-- ) {
        solutions_t                 tmp;
        solutions_t::iterator       lasttokeep;
        solutions_t::const_iterator cur;

        for(cur=p.begin(); cur!=ptr; cur++)
            partial_solve(*cur, tmp, 2, 0);

        lasttokeep = tmp.end();
        if( distance(tmp.begin(), lasttokeep)>500 ) {
            lasttokeep = tmp.begin();
            advance(lasttokeep, 500);
        }
        // replace current list of solutions 'p' with the updated list
        p = solutions_t(tmp.begin(), lasttokeep);
        if( p.size()!=tmp.size() )
            cout << "Kept " << p.size() << " solutions (out of " << tmp.size() << ")" << endl;
    }
    if( ptr!=p.end() )
        return *p.begin();
    return solution_type();
}



// Transform a solution into sourcecode which we can compile
// It takes the size of the block to be compressed as argument
//
// "cmp" = compressed.
// Indicates if numwords is to be interpreted as the compressed blocksize or
// the uncompressed blocksize.
string generate_code(const solution_type& solution, const unsigned int numwords, const bool cmp) {
    // Some basic assertions
    ASSERT_COND( numwords>0 );
    ASSERT_COND( solution.cycle()>0 );
    ASSERT_COND( solution.compressed_cycle()>0 );

    ostringstream              code;
    ostringstream              tmp;
    local_variable             i("i");
    local_variable             tmpsrc("tmpsrc"), tmpdst("tmpdst");
    pointer_variable           srcptr("srcptr"), dstptr("dstptr");
    const unsigned int         cycle( (cmp?(solution.compressed_cycle()):(solution.cycle())) );
    const unsigned int         nloop( numwords/cycle );
    const unsigned int         nremain( numwords%cycle );
    steps_type::const_iterator curstep;

    // we may have a need for this'un a coupla times,
    // the trackmask as a constant value
    tmp << hex_t(solution.mask()) << "ull";
    local_variable     mask( tmp.str() );

    // Start with the compresscode. It will return a pointer
    // one-past the last compressed word, a la STL end iterator:
    //     datatypename* compress_code(datatypename* srcptr) {
    code << "// COMPRESS " << numwords << " " << data_type_name << "\n";
    code << data_type_name << "* compress(" << dstptr.declare() << ") {\n";

    // Declare the variables we could use
    //   We set the srcptr to start-of-block (the argument to our function)
    //   + the number of words to compress less one, such that it is
    //   pointing at the first word to take bits from
    // eBop's extensive testing has ruled that it's always faster to
    // aggregate bits into a tmpdst and when it's filled up write it
    // to the output.
    code << "\t" << srcptr.declare( dstptr+(numwords-1) ) << ";" << endl;
    code << "\t" << tmpdst.declare() << ";\n";
    code << "\t" << tmpsrc.declare() << ";\n";
    code << "\t" << i.declare() << ";\n";
    code << "\n";
    // now we can loop over all compressionsteps
    code << "\tfor(i=0; i<" << nloop << "; i++) {\n";
        bool    usetemp = false;
        for( curstep=solution.begin(); curstep!=solution.end(); curstep++) {
            // we must initialise our tmpdst with a fresh value each time
            // this either is: the first step of the solution, or, the
            // previous step incremented the destination [ie filled one up
            // so we must start with a fresh'un]
            if( curstep==solution.begin() || (curstep-1)->inc_dst)
                code << "\t\t" << tmpdst << " = (" << *dstptr << " & " << mask << ");\n";

            // If this step does not decrement the sourcepointer this implies
            // that the next step takes their bits from the same location. 
            // In this case we use the temp.Retain the value until it's reset
            // to false. The next step should unconditionally use that 
            // temp variable as source if the previous step has filled it.

            // this condition implies we will START using a temp so we must
            // fill the variable with content first
            if( usetemp==false && !curstep->dec_src )
                code << "\t\t" << tmpsrc << " = " << srcptr.ref(variable_type::post_dec_addr) << ";\n";
            // find out where to take our inputbits from
            usetemp = (usetemp || !curstep->dec_src);

            // output the code that moves the bits. the step's compresscode
            // takes care of decrementing the srcptr - if appropriate.
            // Because we aggregate into tmpdest we manage the dstptr
            // ourselves at this level.
            if( usetemp )
                code << "\t\t" << curstep->compress_code(tmpsrc, tmpdst) << endl;
            else
                code << "\t\t" << curstep->compress_code(srcptr, tmpdst) << endl;
            // if the current step did fill up the destword, take care of that
            if( curstep->inc_dst )
                code << "\t\t" << dstptr.ref(variable_type::post_inc_addr) << " = " << tmpdst << ";\n";

            // and if the current step decreased the sourcepointer we reset
            // usetemp to false since it means that it has emptied the temporary
            // (if it used that) or just emptied *srcptr; for the logic use of
            // 'usetemp' that doesn't matter
            if( curstep->dec_src )
                usetemp = false;
        }
    code << "\t} // end of for loop\n";
    // Repeat the loop only for a limited amount of words.
    // Note: we can safely start from scratch since the solution has
    // been constructed in such a way that, after having done one full cycle
    // you end up with no bits in the input and a full word in the output.
    // We interpret "remain" based on our argument "cmprem", such that
    // "cmprem==true" => remain means remaining compressed words. Otherwise
    // "remain" means just the amount of words that still need processing
    curstep = solution.begin();
    usetemp = false;

    if( nremain ) {
        code << "\t// words remaining to process: " << nremain << "\n";

        // embed some information in the code
        if( cmp )
            code << "\t// interpreting them as compressed-words-to-do\n";
        else
            code << "\t// interpreting them as just-words-to-process\n";
    }

    for( unsigned int i=nremain; i>0; ) {
        if( curstep==solution.begin() || (curstep-1)->inc_dst)
            code << "\t" << tmpdst << " = (" << *dstptr << " & " << mask << ");\n";

        // as long as the current step takes bits out of the same src word
        // (ie dec_src==false) - we might better use a temp. if we weren't
        // using a temp yet and this step doesn't empty the src word then
        // it's time to fill the temp. after that 'usetemp' will stay true
        // as long as the steps take bits out of this temp var.
        if( usetemp==false && !curstep->dec_src )
            code << "\t" << tmpsrc << " = " << srcptr.ref(variable_type::post_dec_addr) << ";\n";
        // find out where to take our inputbits from
        usetemp = (usetemp || !curstep->dec_src);

        // output the code that moves the bits. the step's compresscode
        // takes care of decrementing the srcptr - if appropriate.
        // note: eBop tells us that ALWAYS using a temp for destination
        // is faster, hence we do not let the code touch the dstptr, we
        // do that in here
        if( usetemp )
            code << "\t" << curstep->compress_code(tmpsrc, tmpdst) << endl;
        else
            code << "\t" << curstep->compress_code(srcptr, tmpdst) << endl;

        // if the current step did fill up the destword, take care of that
        if( curstep->inc_dst )
            code << "\t" << dstptr.ref(variable_type::post_inc_addr) << " = " << tmpdst << ";\n";

        // If we decrease the sourcepointer we've done another word
        if( curstep->dec_src ) {
            // and if the current step decreased the sourcepointer we reset
            // usetemp to false since it means that it has emptied the temporary
            // (if it used that) or just emptied *srcptr; for the logic use of
            // 'usetemp' that doesn't matter
            usetemp = false;
        }
        if( (cmp==false && (curstep->dec_src || curstep->inc_dst)) ||
            (cmp==true  && curstep->inc_dst) ) {
            i--;
            code << "\t// words remaining to process: " << i << "\n";
        }
        curstep++;
    }
    // if the following condition holds we had some remaining words to
    // compress. Figure out if there may be some bits left that we need to
    // copy out to the destination. Note: curstep!=.begin() implies that
    // there, necessarily, must be a valid previous step! Since the "while
    // (remain)" loop (actually, the "for()" loop ...) unconditionally
    // increments the stepptr at the end of each iteration, curstep points
    // at one step *past* the actual last step taken. We back-up one step to
    // inspect what it did (or didn't)
    if( curstep!=solution.begin() &&
        (curstep-1)->inc_dst==false ) {
        code << "\t// copy out leftover bits\n";
        code << "\t" << dstptr.ref(variable_type::post_inc_addr) << " =  " << tmpdst << ";\n";
    }
    // dstptr points at the first uncompressed word. just great for returnvalue!
    code << "\treturn " << dstptr << ";\n}\n\n";


    //
    // The decompression function. It is a copy of the compresscode only
    // differing in small areas (source <-> dest, + <-> -),
    // hence all comment removed.
    //
    code << data_type_name << "* decompress(" << srcptr.declare() << ") {" << endl;

	code << "\t/* decompress " << numwords << " words */\n";
    code << "\t" << dstptr.declare( srcptr+(numwords-1) ) << ";" << endl;
    code << "\t" << tmpdst.declare() << ";\n";
    code << "\t" << tmpsrc.declare() << ";\n";
    code << "\t" << i.declare() << ";\n";
    code << "\n";
    code << "\tfor(i=0; i<" << nloop << "; i++) {\n";
        usetemp = false;
        for( curstep=solution.begin(); curstep!=solution.end(); curstep++) {
            // under the following conditions we have emptied the sourceword
            // and we must start again from a fresh word sinc if the
            // compress did "inc_dst" it started writing bits to a new word
            if( curstep==solution.begin() || (curstep-1)->inc_dst )
                code << "\t\t" << tmpsrc << " = " << *srcptr << ";\n";

            // as long as the step did not decrement src on compression,
            // it was taking bits out the same location so we keep on
            // writing bits into a temp 
            if( usetemp==false && !curstep->dec_src )
                code << "\t\t" << tmpdst << " ^= " << tmpdst << "; /* fastest set-to-zero */\n";

            usetemp = (usetemp || !curstep->dec_src);

            if( usetemp )
                code << "\t\t" << curstep->decompress_code(tmpsrc, tmpdst) << endl;
            else
                code << "\t\t" << curstep->decompress_code(tmpsrc, dstptr) << endl;

            // dec_src==true => this step emptied the source word on compression.
            // hence, when decompressing, we just filled the word. Take into
            // account the use_temp variable. Since if we wrote to dstptr
            // directly, the decompresscode already modified the dstptr
            if( curstep->dec_src ) {
                // if we were aggregating in a temp, we must still
                // copy out the aggregated value and update the pointer.
                if( usetemp )
                    code << "\t\t" << dstptr.ref(variable_type::post_dec_addr) << " = " << tmpdst << ";\n";
                usetemp = false;
            }

            // if the compression increased the dst this means it had filled
            // up the word on compression, ie we have emptied it on
            // decompression.
            if( curstep->inc_dst ) {
                code << "\t\t// tmpsrc has all 'foreign' bits removed\n";
                if( usetemp || curstep->dec_src )
                    code << "\t\t" << srcptr.ref(variable_type::post_inc_addr) << " = "
                         << "(" << tmpsrc << "&" << mask << ");\n";
            }
        }
    code << "\t} // end of for loop\n";

    curstep = solution.begin();
    usetemp = false;

    if( nremain ) {
        code << "\t// words remaining to decompress: " << nremain << "\n";
        if( cmp )
            code << "\t// interpreting them as compressed-words-to-do\n";
        else
            code << "\t// interpreting them as just-words-to-process\n";
    }
    for( unsigned int i=nremain; i>0; ) {
        if( curstep==solution.begin() || (curstep-1)->inc_dst )
            code << "\t" << tmpsrc << " = " << *srcptr << ";\n";

        if( usetemp==false && !curstep->dec_src )
            code << "\t" << tmpdst << " ^= " << tmpdst << "; /* fastest set-to-zero */\n";

        usetemp = (usetemp || !curstep->dec_src);

        if( usetemp )
            code << "\t" << curstep->decompress_code(tmpsrc, tmpdst) << endl;
        else
            code << "\t" << curstep->decompress_code(tmpsrc, dstptr) << endl;

        if( curstep->dec_src ) {
            if( usetemp )
                code << "\t" << dstptr.ref(variable_type::post_dec_addr) << " = " << tmpdst << ";\n";
            usetemp = false;
        }
        if( curstep->inc_dst ) {
            code << "\t// tmpsrc has all 'foreign' bits removed\n";
            code << "\t" << srcptr.ref(variable_type::post_inc_addr) << " = "
                 << "(" << tmpsrc << "&" << mask << ");\n";
        }
        if( (cmp==false && (curstep->dec_src || curstep->inc_dst)) ||
            (cmp==true  && curstep->inc_dst) ) {
            i--;
            code << "\t" << "// words remaining to process: " << i << "\n";
        }
        // move on to next step
        curstep++;
    }
    if( curstep!=solution.begin() ) {
        if( usetemp ) {
            code << "\t// some destinationbits left in temp\n";
            code << "\t" << dstptr.ref(variable_type::post_dec_addr) << " = " << tmpdst << ";\n";
        } 
        if( (curstep-1)->inc_dst==false ) {
            code << "\t// remove all foreign bits from *srcptr\n";
            code << "\t" << srcptr.ref(variable_type::post_inc_addr) << " = (" << tmpsrc << "&" << mask << ");\n";
        }
    }
    // dstptr points at the first uncompressed word. just great for returnvalue!
    code << "\treturn " << srcptr << ";\n}";

    // and in order to make compilerts happy, we append a newline at the
    // end of the file/code.
    // jeebus. I thought we'd moved on since the 60's .... OF THE
    // SEVENTEENTH CENTURY, YE GODS!
    code << endl;
    return code.str();
}

//
// Bringing it all together
//
compressor_type::compressor_type() {
    this->do_it(solution_type(), 0, false);
}
compressor_type::compressor_type(data_type trackmask, unsigned int numwords) {
    // Great. Try to solve the given problem in at most 100 iterations
    solution_type    solution( solve(solution_type(trackmask), 100) );

    // Check if what we got back was sufficient
    ASSERT2_COND( solution.complete(),
                  SCINFO("could not find a complete solution for "
                         << hex_t(trackmask) << endl) );
    this->do_it(solution, numwords, false);
}
compressor_type::compressor_type(const solution_type& solution, unsigned int numwords, bool cmprem) {
    this->do_it(solution, numwords, cmprem);
}

data_type* compressor_type::compress(data_type* p) const {
    return compress_fn(p);
}
data_type* compressor_type::decompress(data_type* p) const {
    return decompress_fn(p);
}

data_type* compressor_type::do_nothing(data_type* p) {
    return p+compressor_type::blocksize;
}

// "cmp" = compressed. indicates wether the size as indicated by 'numwords'
// is the size after compression or before. it is necessary for the
// codegenerator to know how it should interpret this size.
void compressor_type::do_it(const solution_type& solution, const unsigned int numwords, const bool cmprem) {
    // we do not have to reload etc if someone is requesting the same thing
    // as last and it's already loaded)
    if( (solution.mask()==lastmask) && (numwords==blocksize) && handle )
        return;

    // Darn! Work to do. Let's start by unloading everything -
    // we were called so the caller will expect nothing short
    // of the runtime either having a valid reference to the 
    // new compiled code or nothing
    if( handle ) {
        ASSERT2_COND( ::dlclose(handle)==0,
                      SCINFO("failed to close handle - " << ::dlerror() << endl));
    }
    compress_fn = decompress_fn = &compressor_type::do_nothing;
    handle      = 0;
    blocksize   = numwords;

    if( !solution )
        return;

    // Good. Now let's open the compiler and feed sum generated code to it!
    void*         tmphandle;
    FILE*         fptr;
    const string  generated_filename("/tmp/temp_compress_2");
    const string  code( generate_code(solution, numwords, cmprem) );
    const string  obj( generated_filename + ".o" );
    const string  lib( generated_filename + SOEXT );
    ostringstream compile;
    ostringstream link;

    DEBUG(3, "compressor_type: generated the following code" << endl << code << endl);
    // Let the compiler read from stdin ...
    compile << "gcc" << BOPT << " -fPIC -g -c -Wall -O3 -x c -o " << obj << " -";
    ASSERT2_NZERO( (fptr=::popen(compile.str().c_str(), "w")),
                   SCINFO("popen('" << compile.str() << "' fails - " 
                          << ::strerror(errno)) );
    // Allright - compiler is online, now feed the code straight in!
    ASSERT_COND( ::fwrite(code.c_str(), 1, code.size(), fptr)==code.size() );
    // Close the pipe and check what we got back
    ASSERT_COND( ::pclose(fptr)==0 );

    // Now produce a loadable thingamabob from the objectcode
    link << "gcc" << BOPT << LOPT << " -fPIC -o " << lib << " " << obj;
    ASSERT_ZERO( ::system(link.str().c_str()) );

    // Huzzah! Compil0red and Link0red.
    // Now all that's needed is loading
    ASSERT2_NZERO( (tmphandle=::dlopen(lib.c_str(), RTLD_GLOBAL|RTLD_NOW)),
                   SCINFO(::dlerror() << " opening " << lib << endl) );

    // Now we need to get our dirty hands on tha symbolz.
    if( (compress_fn=(fptr_type)::dlsym(tmphandle, "compress"))==0 ||
        (decompress_fn=(fptr_type)::dlsym(tmphandle, "decompress"))==0 ) {
        // boll0x!
        compress_fn = decompress_fn = (fptr_type)0;
        ASSERT2_COND( compress_fn!=0,
                      SCINFO("failed to load compress/decompress functions!!!!") );
    }
    // update internals only after everything has checked out OK
    handle    = tmphandle; 
    blocksize = numwords;
    lastmask  = solution.mask();
    return;
}

