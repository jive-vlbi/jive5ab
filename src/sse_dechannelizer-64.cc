#include <splitstuff.h>
#include <sse_dechannelizer.h>

using std::make_pair;

#if 0
// Mark K's dechannelization routines have a different calling sequence than
// we do, fix that in here
void marks_2Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1) {
    extract_2Ch2bit1to2(block, d0, d1, blocksize/2);
}

void marks_4Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3) {
    extract_4Ch2bit1to2(block, d0, d1, d2, d3, blocksize/4);
}

void marks_8Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                            void* d4, void* d5, void* d6, void* d7) {
    extract_8Ch2bit1to2(block, d0, d1, d2, d3, d4, d5, d6, d7, blocksize/8);
}

void marks_8Ch2bit(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                        void* d4, void* d5, void* d6, void* d7) {
    extract_8Ch2bit(block, d0, d1, d2, d3, d4, d5, d6, d7, blocksize/8);
}

void marks_16Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                             void* d4, void* d5, void* d6, void* d7,
                                                             void* d8, void* d9, void* d10, void* d11, 
                                                             void* d12, void* d13, void* d14, void* d15) {
    extract_16Ch2bit1to2(block, d0, d1, d2, d3, d4, d5, d6, d7,
                                d8, d9, d10, d11, d12, d13, d14, d15, blocksize/16);
}
void harros_16Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                              void* d4, void* d5, void* d6, void* d7,
                                                              void* d8, void* d9, void* d10, void* d11, 
                                                              void* d12, void* d13, void* d14, void* d15) {
    extract_16Ch2bit1to2_hv(block, blocksize/16, d0, d1, d2, d3, d4, d5, d6, d7,
                                                 d8, d9, d10, d11, d12, d13, d14, d15);
}
#endif

// All available splitfunctions go here
functionmap_type mk_functionmap( void ) {
    functionmap_type               rv;
    function_caster<splitfunction> caster;

#if 0
    SPLITASSERT( rv.insert(make_pair("2Ch2bit1to2",
                                     splitproperties_type("extract_2Ch2bit1to2",
                                                          caster(&marks_2Ch2bit1to2),
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("4Ch2bit1to2",
                                     splitproperties_type("extract_4Ch2bit1to2",
                                                          caster(&marks_4Ch2bit1to2),
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit1to2",
                                     splitproperties_type("extract_8Ch2bit1to2",
                                                          caster(&marks_8Ch2bit1to2),
                                                          8))).second );
#endif
    SPLITASSERT( rv.insert(make_pair("8Ch2bit1to2_hv",
                                     splitproperties_type("extract_8Ch2bit1to2_hv",
                                                          caster(&extract_8Ch2bit1to2_hv)/*harros_8Ch2bit1to2*/,
                                                          8))).second );
    // Insert this one as alias
    SPLITASSERT( rv.insert(make_pair("8Ch2bit",
                                     splitproperties_type("extract_8Ch2bit",
                                                          caster(&extract_8Ch2bit_hv),
                                                          //caster(&marks_8Ch2bit),
                                                          8))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit_hv",
                                     splitproperties_type("extract_8Ch2bit_hv",
                                                          caster(&extract_8Ch2bit_hv),
                                                          8))).second );
#if 0
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2",
                                     splitproperties_type("extract_16Ch2bit1to2",
                                                          caster(&marks_16Ch2bit1to2),
                                                          16))).second );
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2_hv",
                                     splitproperties_type("extract_16Ch2bit1to2_hv",
                                                          caster(&harros_16Ch2bit1to2),
                                                          16))).second );
#endif
    SPLITASSERT( rv.insert(make_pair("16bitx2",
                                     splitproperties_type("split16bitby2",
                                                          caster(&split16bitby2),
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("16bitx4",
                                     splitproperties_type("split16bitby4",
                                                          caster(&split16bitby4),
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("8bitx4",
                                     splitproperties_type("split8bitby4",
                                                          caster(&split8bitby4),
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("32bitx2",
                                     splitproperties_type("split32bitby2",
                                                          caster(&split32bitby2),
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("swap_sign_mag",
                                     splitproperties_type("swap sign/mag",
                                                          caster(&swap_sign_mag),
                                                          1))).second );
    return rv;
}
