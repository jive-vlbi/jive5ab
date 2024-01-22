#include <splitstuff.h>
#include <evlbidebug.h>

using std::make_pair;

/* NOTE: CALL SEQUENCE
 *       src, len, dst0, dst1, ...
 */
void split8bitby4(void* src, size_t len, void* dst0, void* dst1, void* dst2, void* dst3) {
    union destruct_type {
        uint64_t        qw1;
        uint8_t         b0, b1, b2, b3, b4, b5, b6, b7;
    } tmp;
    uint64_t const* u64src = static_cast<uint64_t const*>(src);
    uint64_t const* u64end = u64src + len/sizeof(uint64_t) - 1;
    uint8_t*        d0     = static_cast<uint8_t*>(dst0);
    uint8_t*        d1     = static_cast<uint8_t*>(dst1);
    uint8_t*        d2     = static_cast<uint8_t*>(dst2);
    uint8_t*        d3     = static_cast<uint8_t*>(dst3);
DEBUG(0, "split8bitby4: len=" << len << " dst0..3=" << dst0 << "," << dst1 << "," << dst2 << "," << dst3 << std::endl);

    while( u64src<u64end ) {
        tmp.qw1 = *u64src;
        *d0++   = tmp.b0;
        *d1++   = tmp.b1;
        *d2++   = tmp.b2;
        *d3++   = tmp.b3;
        *d0++   = tmp.b4;
        *d1++   = tmp.b5;
        *d2++   = tmp.b6;
        *d3++   = tmp.b7;
        u64src++;
    }
}
void split16bitby2(void* src, size_t len, void* dst0, void* dst1) {
    union destruct_type {
        uint64_t        qw1;
        uint16_t        w0, w1, w2, w3;
    } tmp;
    uint64_t const* u64src = static_cast<uint64_t const*>(src);
    uint64_t const* u64end = u64src + len/sizeof(uint64_t) - 1;
    uint16_t*        d0     = static_cast<uint16_t*>(dst0);
    uint16_t*        d1     = static_cast<uint16_t*>(dst1);

    while( u64src<u64end ) {
        tmp.qw1 = *u64src;
        *d0++   = tmp.w0;
        *d1++   = tmp.w1;
        *d0++   = tmp.w2;
        *d1++   = tmp.w3;
        u64src++;
    }
}
void split16bitby4(void* src, size_t len, void* dst0, void* dst1, void* dst2, void* dst3) {
    union destruct_type {
        uint64_t        qw1;
        uint16_t        w0, w1, w2, w3;
    } tmp;
    uint64_t const* u64src = static_cast<uint64_t const*>(src);
    uint64_t const* u64end = u64src + len/sizeof(uint64_t) - 1;
    uint16_t*        d0     = static_cast<uint16_t*>(dst0);
    uint16_t*        d1     = static_cast<uint16_t*>(dst1);
    uint16_t*        d2     = static_cast<uint16_t*>(dst2);
    uint16_t*        d3     = static_cast<uint16_t*>(dst3);

    while( u64src<u64end ) {
        tmp.qw1 = *u64src;
        *d0++   = tmp.w0;
        *d1++   = tmp.w1;
        *d2++   = tmp.w2;
        *d3++   = tmp.w3;
        u64src++;
    }
}
void split32bitby2(void* src, size_t len, void* dst0, void* dst1) {
    union destruct_type {
        uint64_t        qw1;
        uint32_t        dw0, dw1;
    } tmp;
    uint64_t const* u64src = static_cast<uint64_t const*>(src);
    uint64_t const* u64end = u64src + len/sizeof(uint64_t) - 1;
    uint32_t*        d0    = static_cast<uint32_t*>(dst0);
    uint32_t*        d1    = static_cast<uint32_t*>(dst1);


    while( u64src<u64end ) {
        tmp.qw1 = *u64src;
        *d0++   = tmp.dw0;
        *d1++   = tmp.dw1;
        u64src++;
    }
}

#if 0
void extract_16Ch2bit1to2_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7,
		void *dst8, void *dst9, void *dst10, void *dst11,
        void *dst12,void *dst13, void *dst14, void *dst15) asm("extract_16Ch2bit1to2_hv");
void extract_8Ch2bit1to2_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7 ) asm("extract_8Ch2bit1to2_hv");
void extract_8Ch2bit_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7 ) asm("extract_8Ch2bit_hv");
#endif

/* This is not so much a splitter as it is a bitswapper -
 * changes standard astronomy Mark5B mode data sign/mag
 * bits into appropriate VDIF bitorder */
void swap_sign_mag(void* src, size_t len, void* dst0) {
    uint64_t const* u64src = static_cast<uint64_t const*>(src);
    uint64_t const* u64end = u64src + len/sizeof(uint64_t) - 1;
    uint64_t*       d0    = static_cast<uint64_t*>(dst0);
    uint64_t        tmp1;
    uint64_t const  signs = 0x55555555L;
    uint64_t const  mags  = ~signs;

    while( u64src<u64end ) {
        tmp1  = *u64src;
        *d0++ = (((tmp1 & signs)<<1) | ((tmp1 & mags)>>1));
        u64src++;
    }
}

void do_nothing(void* src, size_t len, void* dst0) {
DEBUG(0, "do_nothing: len=" << len << ", dst0=" << dst0 << std::endl);
    //::memcpy(dst0, src, len);
    *((uint64_t*)dst0) = *((uint64_t*)src);
}

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
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2",
                                     splitproperties_type("extract_16Ch2bit1to2",
                                                          caster(&marks_16Ch2bit1to2),
                                                          16))).second );
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2_hv",
                                     splitproperties_type("extract_16Ch2bit1to2_hv",
                                                          caster(&harros_16Ch2bit1to2),
                                                          16))).second );
#endif
    SPLITASSERT( rv.insert(make_pair("16bitx2-t",
                                     splitproperties_type("split16bitby2",
                                                          caster(&split16bitby2),
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("16bitx4-t",
                                     splitproperties_type("split16bitby4",
                                                          caster(&split16bitby4),
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("8bitx4-t",
                                     splitproperties_type("split8bitby4",
                                                          caster(&split8bitby4),
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("32bitx2-t",
                                     splitproperties_type("split32bitby2",
                                                          caster(&split32bitby2),
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("swap_sign_mag-t",
                                     splitproperties_type("swap sign/mag",
                                                          caster(&swap_sign_mag),
                                                          1))).second );
    SPLITASSERT( rv.insert(make_pair("do_nothing",
                                     splitproperties_type("no-op",
                                                          caster(&do_nothing),
                                                          1))).second );
    return rv;
}
