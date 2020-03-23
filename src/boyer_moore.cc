#include <boyer_moore.h>
#include <string.h>  // for ::memcpy()
#include <algorithm> // for std::max()
#include <limits.h>  // for INT_MAX

#define ALPHABET_SIZE 256

struct needle_longer_than_INT_MAX {};
struct haystack_longer_than_INT_MAX {};

boyer_moore::boyer_moore() :
    badcharacter( 0 ), goodsuffix( 0 ), needle_len( 0 ), needle( 0 )
{}

boyer_moore::boyer_moore(void const* pat, unsigned int patlength) :
    badcharacter( (patlength && patlength<(unsigned int)INT_MAX)?(new int[ALPHABET_SIZE]):(0) ),
    goodsuffix( (patlength && patlength<(unsigned int)INT_MAX)?(new int[patlength+1]):(0) ),
    needle_len( patlength ),
    needle( (patlength && patlength<(unsigned int)INT_MAX)?(new char[patlength]):(0) ) {
        // support patternlength of 0 but really throw up when the pattern
        // becomes too big
        if( patlength>=(unsigned int)INT_MAX )
            throw needle_longer_than_INT_MAX();

        // Initialize heuristics
        if( needle_len ) {
            ::memcpy(needle, pat, needle_len);
            boyer_moore::prepare_badcharacter_heuristic(needle, (int)needle_len, badcharacter);
            boyer_moore::prepare_goodsuffix_heuristic(needle, (int)needle_len, goodsuffix);
        }
}

boyer_moore::boyer_moore(const boyer_moore& other):
    badcharacter( (other.badcharacter==0)?(0):(new int[ALPHABET_SIZE]) ),
    goodsuffix( (other.goodsuffix==0)?(0):(new int[other.needle_len+1]) ),
    needle_len( other.needle_len ),
    needle( (other.needle==0)?(0):(new char[other.needle_len]) ) {
        // copy everything copyable
        if( other.badcharacter )
            ::memcpy(badcharacter, other.badcharacter, ALPHABET_SIZE*sizeof(int));
        if( other.goodsuffix )
            ::memcpy(goodsuffix, other.goodsuffix, (needle_len+1)*sizeof(int));
        if( other.needle )
            ::memcpy(needle, other.needle, needle_len);
}

const boyer_moore& boyer_moore::operator=(const boyer_moore& other) {
    if( this!=&other ) {
        // clean out ourselves, meanwhile copying the copyable stuff from
        // other
        needle_len = other.needle_len;

        // take care of badcharcter copying
        if( other.badcharacter ) {
            if( !badcharacter )
                badcharacter = new int[ALPHABET_SIZE];
            ::memcpy(badcharacter, other.badcharacter, ALPHABET_SIZE*sizeof(int));
        } else {
            delete [] badcharacter;
            badcharacter = 0;
        }

        // Same for goodsuffix
        if( other.goodsuffix ) {
            if( !goodsuffix )
                goodsuffix = new int[needle_len+1];
            ::memcpy(goodsuffix, other.goodsuffix, (needle_len+1)*sizeof(int));
        } else {
            delete [] goodsuffix;
            goodsuffix   = 0;
        }

        // and the needle
        if( other.needle ) {
            if( !needle )
                needle = new char[needle_len];
            ::memcpy(needle, other.needle, needle_len);
        } else {
            delete [] needle;
            needle   = 0;
        }
    }
    return *this;
}

void const* boyer_moore::operator()(void const* const haystack, unsigned int haystack_len) {
    return (void const* )this->operator()((char const*)haystack, haystack_len);
}

char const* boyer_moore::operator()(char const* const haystack, unsigned int haystack_len) {
    int               i,j;
    const int         m = (int)needle_len;
    const int         n = (int)haystack_len;

    if( haystack_len>=(unsigned int)INT_MAX )
            throw haystack_longer_than_INT_MAX();
    if( haystack_len==0 )
        return 0;
    if( needle_len==0 )
        return 0;
    if( needle_len>haystack_len )
        return 0;

    /** Boyer-Moore search */
    j = 0;
    while( j<=(n-m) ) {
        for( i=m-1; i>=0 && needle[i]==haystack[i+j]; --i) {};

        if( i<0 )
            return haystack+j;
        else
            j += std::max(goodsuffix[i], badcharacter[(unsigned char)haystack[i+j]]-m+1+i);
    }
    return 0;
}

unsigned char const* boyer_moore::operator()(unsigned char const* const haystack, unsigned int haystacklen) {
    return (unsigned char const*)this->operator()((char const*)haystack, haystacklen);
}

boyer_moore::~boyer_moore() {
    delete [] badcharacter;
    delete [] goodsuffix;
    delete [] needle;
}



// static methods
void boyer_moore::prepare_badcharacter_heuristic(char const* n, int n_len, int* result) {
    int i;
    for( i=0; i<ALPHABET_SIZE; ++i )
        result[i] = n_len;
    for( i=0; i<n_len-1; ++i )
        result[(unsigned char)n[i]] = n_len - i - 1;
}

void boyer_moore::suffixes(char const* n, int n_len, int *result) {
    int f, g, i;

    result[n_len - 1] = n_len;
    f = g = n_len - 1;
    for( i=n_len-2; i>=0; --i ) {
        if( i>g && result[i + n_len - 1 - f]<(i - g) ) {
            result[i] = result[i + n_len - 1 - f];
        } else {
            if( i<g )
                g = i;
            f = i;
            while( g>=0 && n[g]==n[g + n_len - 1 - f] )
                --g;
            result[i] = f - g;
        }
    }
}

void boyer_moore::prepare_goodsuffix_heuristic(char const* n, int n_len, int* result) {
    int  i, j;
    int* suff = new int[n_len+1];

    suffixes(n, n_len, suff);

    for( i=0; i<n_len; ++i )
        result[i] = n_len;

    j = 0;
    for( i=n_len-1; i>=-1; --i )
        if( i==-1 || suff[i]==i+1 )
            for( ; j<n_len-1-i; ++j )
                if( result[j]==n_len )
                    result[j] = n_len-1-i;

    for( i=0; i<=n_len-2; ++i )
        result[n_len - 1 - suff[i]] = n_len - 1 - i;

    delete [] suff;
}


