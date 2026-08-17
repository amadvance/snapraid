/*
 * SpookyHash V2
 * By Bob Jenkins, public domain.
 *
 * SnapRAID long-path variant.
 *
 * SnapRAID deliberately does not use SpookyHash's Short() optimization for
 * messages shorter than 192 bytes. All input lengths are processed through
 * the 12-word (96-byte) long path, matching cmdline/spooky2.c.
 *
 * SnapRAID uses a 128-bit seed. SMHasher3 supplies one 64-bit seed, so this
 * adapter uses the same value for both 64-bit seed halves. This is the same
 * convention used by SMHasher3's regular SpookyHash adapter.
 */

#include "Platform.h"
#include "Hashlib.h"

//------------------------------------------------------------------------------

static const size_t   SPOOKY_NUM_VARS   = 12;
static const size_t   SPOOKY_BLOCK_SIZE = SPOOKY_NUM_VARS * 8;
static const uint64_t SPOOKY_CONST      = UINT64_C(0xdeadbeefdeadbeef);

template <bool bswap>
static FORCE_INLINE void SpookyMix(
        const uint8_t * data,
        uint64_t & s0, uint64_t & s1, uint64_t & s2, uint64_t & s3,
        uint64_t & s4, uint64_t & s5, uint64_t & s6, uint64_t & s7,
        uint64_t & s8, uint64_t & s9, uint64_t & s10, uint64_t & s11) {

    s0  += GET_U64<bswap>(data, 8 *  0);  s2 ^= s10;  s11 ^= s0;   s0  = ROTL64(s0, 11);  s11 += s1;
    s1  += GET_U64<bswap>(data, 8 *  1);  s3 ^= s11;   s0 ^= s1;   s1  = ROTL64(s1, 32);   s0 += s2;
    s2  += GET_U64<bswap>(data, 8 *  2);  s4 ^= s0;    s1 ^= s2;   s2  = ROTL64(s2, 43);   s1 += s3;
    s3  += GET_U64<bswap>(data, 8 *  3);  s5 ^= s1;    s2 ^= s3;   s3  = ROTL64(s3, 31);   s2 += s4;
    s4  += GET_U64<bswap>(data, 8 *  4);  s6 ^= s2;    s3 ^= s4;   s4  = ROTL64(s4, 17);   s3 += s5;
    s5  += GET_U64<bswap>(data, 8 *  5);  s7 ^= s3;    s4 ^= s5;   s5  = ROTL64(s5, 28);   s4 += s6;
    s6  += GET_U64<bswap>(data, 8 *  6);  s8 ^= s4;    s5 ^= s6;   s6  = ROTL64(s6, 39);   s5 += s7;
    s7  += GET_U64<bswap>(data, 8 *  7);  s9 ^= s5;    s6 ^= s7;   s7  = ROTL64(s7, 57);   s6 += s8;
    s8  += GET_U64<bswap>(data, 8 *  8); s10 ^= s6;    s7 ^= s8;   s8  = ROTL64(s8, 55);   s7 += s9;
    s9  += GET_U64<bswap>(data, 8 *  9); s11 ^= s7;    s8 ^= s9;   s9  = ROTL64(s9, 54);   s8 += s10;
    s10 += GET_U64<bswap>(data, 8 * 10);  s0 ^= s8;    s9 ^= s10;  s10 = ROTL64(s10, 22);  s9 += s11;
    s11 += GET_U64<bswap>(data, 8 * 11);  s1 ^= s9;   s10 ^= s11;  s11 = ROTL64(s11, 46); s10 += s0;
}

static FORCE_INLINE void SpookyEndPartial(
        uint64_t & h0, uint64_t & h1, uint64_t & h2, uint64_t & h3,
        uint64_t & h4, uint64_t & h5, uint64_t & h6, uint64_t & h7,
        uint64_t & h8, uint64_t & h9, uint64_t & h10, uint64_t & h11) {

    h11 += h1;   h2 ^= h11;  h1  = ROTL64(h1, 44);
    h0  += h2;   h3 ^= h0;   h2  = ROTL64(h2, 15);
    h1  += h3;   h4 ^= h1;   h3  = ROTL64(h3, 34);
    h2  += h4;   h5 ^= h2;   h4  = ROTL64(h4, 21);
    h3  += h5;   h6 ^= h3;   h5  = ROTL64(h5, 38);
    h4  += h6;   h7 ^= h4;   h6  = ROTL64(h6, 33);
    h5  += h7;   h8 ^= h5;   h7  = ROTL64(h7, 10);
    h6  += h8;   h9 ^= h6;   h8  = ROTL64(h8, 13);
    h7  += h9;  h10 ^= h7;   h9  = ROTL64(h9, 38);
    h8  += h10; h11 ^= h8;   h10 = ROTL64(h10, 53);
    h9  += h11;  h0 ^= h9;   h11 = ROTL64(h11, 42);
    h10 += h0;   h1 ^= h10;  h0  = ROTL64(h0, 54);
}

template <bool bswap>
static FORCE_INLINE void SpookyEnd(
        const uint8_t * data,
        uint64_t & h0, uint64_t & h1, uint64_t & h2, uint64_t & h3,
        uint64_t & h4, uint64_t & h5, uint64_t & h6, uint64_t & h7,
        uint64_t & h8, uint64_t & h9, uint64_t & h10, uint64_t & h11) {

    h0  += GET_U64<bswap>(data, 8 *  0);
    h1  += GET_U64<bswap>(data, 8 *  1);
    h2  += GET_U64<bswap>(data, 8 *  2);
    h3  += GET_U64<bswap>(data, 8 *  3);
    h4  += GET_U64<bswap>(data, 8 *  4);
    h5  += GET_U64<bswap>(data, 8 *  5);
    h6  += GET_U64<bswap>(data, 8 *  6);
    h7  += GET_U64<bswap>(data, 8 *  7);
    h8  += GET_U64<bswap>(data, 8 *  8);
    h9  += GET_U64<bswap>(data, 8 *  9);
    h10 += GET_U64<bswap>(data, 8 * 10);
    h11 += GET_U64<bswap>(data, 8 * 11);

    SpookyEndPartial(h0, h1, h2, h3, h4, h5,
                     h6, h7, h8, h9, h10, h11);
    SpookyEndPartial(h0, h1, h2, h3, h4, h5,
                     h6, h7, h8, h9, h10, h11);
    SpookyEndPartial(h0, h1, h2, h3, h4, h5,
                     h6, h7, h8, h9, h10, h11);
}

//------------------------------------------------------------------------------
// Exact SnapRAID SpookyHash2 long-path algorithm.
//
// Unlike the upstream SpookyHash2 one-shot Hash128(), this function NEVER
// switches to Short() for len < 192. This is intentional and is the defining
// behavior of the SnapRAID variant.

template <bool bswap>
static NEVER_INLINE void SpookyHash2SnapRAIDCore(
        const void * data,
        const size_t size,
        const uint64_t seed_a,
        const uint64_t seed_b,
        uint64_t * out_lo,
        uint64_t * out_hi) {

    uint64_t h0, h1, h2, h3, h4, h5;
    uint64_t h6, h7, h8, h9, h10, h11;

    h9  = seed_a;
    h10 = seed_b;

    h0 = h3 = h6 = h9;
    h1 = h4 = h7 = h10;
    h2 = h5 = h8 = h11 = SPOOKY_CONST;

    const uint8_t * p = (const uint8_t *)data;
    size_t remaining = size;

    while (remaining >= SPOOKY_BLOCK_SIZE) {
        SpookyMix<bswap>(p,
                h0, h1, h2, h3, h4, h5,
                h6, h7, h8, h9, h10, h11);
        p += SPOOKY_BLOCK_SIZE;
        remaining -= SPOOKY_BLOCK_SIZE;
    }

    /*
     * SnapRAID tail:
     *   - copy 0..95 remainder bytes
     *   - zero-fill the rest
     *   - store the remainder length in byte 95
     *   - run SpookyHash2 End() directly
     */
    alignas(16) uint8_t buf[SPOOKY_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    if (remaining > 0) {
        memcpy(buf, p, remaining);
    }
    buf[SPOOKY_BLOCK_SIZE - 1] = (uint8_t)remaining;

    SpookyEnd<bswap>(buf,
            h0, h1, h2, h3, h4, h5,
            h6, h7, h8, h9, h10, h11);

    *out_lo = h0;
    *out_hi = h1;
}

//------------------------------------------------------------------------------

/*
 * SMHasher3 supplies one 64-bit seed, while SnapRAID supplies two independent
 * 64-bit halves. Duplicate the SMHasher3 seed into both halves, matching the
 * convention used by SMHasher3's regular SpookyHash adapter.
 */
template <bool bswap>
static void SpookyHash2SnapRAID(
        const void * in,
        const size_t len,
        const seed_t seed,
        void * out) {

    uint64_t out_lo;
    uint64_t out_hi;

    SpookyHash2SnapRAIDCore<bswap>(
            in,
            len,
            (uint64_t)seed,
            (uint64_t)seed,
            &out_lo,
            &out_hi);

    /* SnapRAID stores the 128-bit digest in canonical little-endian order. */
    out_lo = COND_BSWAP(out_lo, isBE());
    out_hi = COND_BSWAP(out_hi, isBE());

    PUT_U64<false>(out_lo, (uint8_t *)out, 0);
    PUT_U64<false>(out_hi, (uint8_t *)out, 8);
}

//------------------------------------------------------------------------------

REGISTER_FAMILY(spooky2_snapraid,
   $.src_url    = "https://github.com/amadvance/snapraid/",
   $.src_status = HashFamilyInfo::SRC_ACTIVE
 );

REGISTER_HASH(SpookyHash2_128_snapraid,
   $.desc =
         "SpookyHash v2 128-bit, SnapRAID long-path-only variant",

   $.hash_flags =
         FLAG_HASH_ENDIAN_INDEPENDENT,

   $.impl_flags =
         FLAG_IMPL_ROTATE |
         FLAG_IMPL_CANONICAL_LE |
         FLAG_IMPL_LICENSE_GPL3,

   $.bits = 128,

   $.verification_LE = 0x965BDDF8,
   $.verification_BE = 0x2EF470E2,

   $.hashfn_native = SpookyHash2SnapRAID<false>,
   $.hashfn_bswap  = SpookyHash2SnapRAID<true>
 );
