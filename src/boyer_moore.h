// implementation of fast stringsearch
#ifndef JIVE5A_BOYER_MOORE_H
#define JIVE5A_BOYER_MOORE_H

// http://en.wikipedia.org/wiki/Boyer%E2%80%93Moore_string_search_algorithm 
//
// We adapt it in such a way that for a stringsearch one first creates a
// searchobject (which will precompute & store the jumptables). 
//
// This precompiled searchitem can be given a memoryarea to look for the
// pattern.
//
// It is not unlike regex matching where first one compiles the pattern
// and then can search for it any number of times.
//
// This is primarily done because for *our* [(e-)VLBI] purposes we must
// look over and over again for the same pattern. 'Compiling the pattern' on
// every search would be FAR too expensive.
//
// NOTE NOTE NOTE NOTE NOTE IMPORTANT IMPORTANT IMPORTANT
//
//       this stringsearch is ONLY SAFE FOR STRINGS/PATTERNS
//       up to 2GB each!!!!
//       (whatever your INT_MAX-1 evaluates to, more specifically)
//       due to an int/unsigned conversion inside.
//
//       The code will throw an exception when either the needle
//       is >= INT_MAX or, when attempting to search, the haystack
//       is >= INT_MAX.
//

struct boyer_moore {
    public:
        // Acceptable c'tors: default, full-fledged + copy
        boyer_moore();
        boyer_moore(void const* pattern, unsigned int patlength);
        boyer_moore(const boyer_moore& other);
        const boyer_moore& operator=(const boyer_moore& other);

        // these are the search functions.
        // return pointer-to-first-character-of-pattern-in-haystack if the
        // pattern occurs or NULL/0 if it doesn't
        void const*          operator()(void const* const haystack, unsigned int haystack_len);
        char const*          operator()(char const* const haystack, unsigned int haystack_len);
        unsigned char const* operator()(unsigned char const* const haystack, unsigned int haystack_len);

        ~boyer_moore(); 

    private:
        // the jumptables
        int*           badcharacter;
        int*           goodsuffix;
        unsigned int   needle_len;
        char*          needle;

        // static methods
        static void    prepare_badcharacter_heuristic(char const* needle, int needle_len, int* result);
        static void    prepare_goodsuffix_heuristic(char const* needle, int needle_len, int* result);
        static void    suffixes(char const* needle, int needle_len, int* result);
};

#endif
