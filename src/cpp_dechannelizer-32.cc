#include <splitstuff.h>
#include <evlbidebug.h>

using std::make_pair;

/* NOTE: CALL SEQUENCE
 *       src, len, dst0, dst1, ...
 */
void split8bitby4_2(void* src, unsigned int len, void* dst0, void* dst1, void* dst2, void* dst3) {
    uint8_t const*  u8src = static_cast<uint8_t const*>(src);
    uint8_t const* const u8end = u8src + len;
    uint8_t*        d0     = static_cast<uint8_t*>(dst0);
    uint8_t*        d1     = static_cast<uint8_t*>(dst1);
    uint8_t*        d2     = static_cast<uint8_t*>(dst2);
    uint8_t*        d3     = static_cast<uint8_t*>(dst3);

    while( u8src<u8end ) {
        *d0++  = *u8src++;
        *d1++  = *u8src++;
        *d2++  = *u8src++;
        *d3++  = *u8src++;
    }
}

functionmap_type mk_functionmap( void ) {
    functionmap_type               rv;
    function_caster<splitfunction> caster;

    SPLITASSERT( rv.insert(make_pair("8bitx4-t2",
                                     splitproperties_type("split8bitby4",
                                                          caster(&split8bitby4_2),
                                                          4))).second );
    return rv;
}
