/*
 * MuseAir v2
 * By K--Aethiax
 *
 * SnapRAID BFast-128 long-path variant.
 *
 * For len < 32, the input is zero-padded to 32 bytes and processed
 * through the long path. The original length is preserved for
 * finalization.
 *
 * Released into the public domain under the CC0 1.0 license.
 */

#include "Platform.h"
#include "Hashlib.h"
#include "Mathmult.h"

//------------------------------------------------------------

#define U64x(N) ((N) * 8)

/* AiryAi(0) fractional part calculated by Y-Cruncher */
static const uint64_t MUSEAIR_CONSTANT[13] = {
    UINT64_C(0x5ae31e589c56e17a),
    UINT64_C(0x96d7bb04e64f6da9),
    UINT64_C(0x7ab1006b26f9eb64),
    UINT64_C(0x21233394220b8457),
    UINT64_C(0x047cb9557c9f3b43),
    UINT64_C(0xd24f2590c0bcee28),
    UINT64_C(0x33ea8f71bb6016d8),
    UINT64_C(0xb5d2697595d0a01f),
    UINT64_C(0x9bb30a32f00e2b4f),
    UINT64_C(0x4acea09317a429d1),
    UINT64_C(0xc2b2435dfdd545c6),
    UINT64_C(0xfda811a785572a42),
    UINT64_C(0xe5f50676bf67137b)
};

static const uint64_t MASK_I = UINT64_C(01555555555555555555555);
static const uint64_t MASK_J = UINT64_C(01333333333333333333333);
static const uint64_t MASK_K = UINT64_C(00666666666666666666666);

//------------------------------------------------------------

template <bool bswap>
static NEVER_INLINE void museair_hash_snapraid(
        const uint8_t * bytes,
        const size_t len,
        const seed_t seed_a,
        const seed_t seed_b,
        uint64_t * out_lo,
        uint64_t * out_hi) {

    const uint8_t * p = bytes;
    size_t q = len;

    uint8_t buf[32];

    /*
     * SnapRAID extension:
     * zero-pad short inputs and use the MuseAir long path.
     *
     * Keep 'len' unchanged because the original length is mixed
     * into finalization.
     */
    if (unlikely(q < U64x(4))) {
        memset(buf, 0, sizeof(buf));

        if (q > 0) {
            memcpy(buf, p, q);
        }

        p = buf;
        q = U64x(4);
    }

    uint64_t i, j, k;

    uint64_t lo0, lo1, lo2, lo3, lo4;
    uint64_t lo5 = MUSEAIR_CONSTANT[6];

    uint64_t hi0, hi1, hi2, hi3, hi4;
    uint64_t hi5 = MUSEAIR_CONSTANT[6];

    uint64_t state[6] = {
        MUSEAIR_CONSTANT[0],
        MUSEAIR_CONSTANT[1],
        MUSEAIR_CONSTANT[2],
        MUSEAIR_CONSTANT[3],
        MUSEAIR_CONSTANT[4],
        MUSEAIR_CONSTANT[5]
    };

    /*
     * MuseAir v2 BFast-128 seed initialization.
     */
    state[0] ^= seed_a & MASK_I;
    state[1] ^= seed_b & MASK_J;
    state[2] ^= seed_a & MASK_K;
    state[3] ^= seed_b & MASK_I;
    state[4] ^= seed_a & MASK_J;
    state[5] ^= seed_b & MASK_K;

    /*
     * Main 96-byte loop.
     *
     * Note the strict > rather than >=. This is intentional and
     * matches MuseAir v2 / SnapRAID.
     */
    if (unlikely(q > U64x(12))) {
        do {
            state[0] ^= GET_U64<bswap>(p, U64x(0));
            state[1] ^= GET_U64<bswap>(p, U64x(1));
            MathMult::mult64_128(lo0, hi0, state[0], state[1]);
            state[0] = lo5 ^ hi0;

            state[1] ^= GET_U64<bswap>(p, U64x(2));
            state[2] ^= GET_U64<bswap>(p, U64x(3));
            MathMult::mult64_128(lo1, hi1, state[1], state[2]);
            state[1] = lo0 ^ hi1;

            state[2] ^= GET_U64<bswap>(p, U64x(4));
            state[3] ^= GET_U64<bswap>(p, U64x(5));
            MathMult::mult64_128(lo2, hi2, state[2], state[3]);
            state[2] = lo1 ^ hi2;

            state[3] ^= GET_U64<bswap>(p, U64x(6));
            state[4] ^= GET_U64<bswap>(p, U64x(7));
            MathMult::mult64_128(lo3, hi3, state[3], state[4]);
            state[3] = lo2 ^ hi3;

            state[4] ^= GET_U64<bswap>(p, U64x(8));
            state[5] ^= GET_U64<bswap>(p, U64x(9));
            MathMult::mult64_128(lo4, hi4, state[4], state[5]);
            state[4] = lo3 ^ hi4;

            state[5] ^= GET_U64<bswap>(p, U64x(10));
            state[0] ^= GET_U64<bswap>(p, U64x(11));
            MathMult::mult64_128(lo5, hi5, state[5], state[0]);
            state[5] = lo4 ^ hi5;

            p += U64x(12);
            q -= U64x(12);

        } while (likely(q > U64x(12)));

        state[0] ^= lo5;
    }

    /*
     * Tail.
     */
    lo0 = 0;
    lo1 = 0;
    lo2 = 0;
    lo3 = 0;

    hi0 = state[1];
    hi1 = state[2];
    hi2 = state[3];
    hi3 = state[4];

    if (q > U64x(4)) {
        state[0] ^= GET_U64<bswap>(p, U64x(0));
        state[1] ^= GET_U64<bswap>(p, U64x(1));
        MathMult::mult64_128(lo0, hi0, state[0], state[1]);

        if (q > U64x(6)) {
            state[1] ^= GET_U64<bswap>(p, U64x(2));
            state[2] ^= GET_U64<bswap>(p, U64x(3));
            MathMult::mult64_128(lo1, hi1, state[1], state[2]);

            if (q > U64x(8)) {
                state[2] ^= GET_U64<bswap>(p, U64x(4));
                state[3] ^= GET_U64<bswap>(p, U64x(5));
                MathMult::mult64_128(lo2, hi2, state[2], state[3]);

                if (q > U64x(10)) {
                    state[3] ^= GET_U64<bswap>(p, U64x(6));
                    state[4] ^= GET_U64<bswap>(p, U64x(7));
                    MathMult::mult64_128(lo3, hi3,
                            state[3], state[4]);
                }
            }
        }
    }

    /*
     * Always mix the final 32 bytes.
     */
    state[4] ^= GET_U64<bswap>(p + q - U64x(4), 0);
    state[5] ^= GET_U64<bswap>(p + q - U64x(3), 0);
    MathMult::mult64_128(lo4, hi4, state[4], state[5]);

    state[5] ^= GET_U64<bswap>(p + q - U64x(2), 0);
    state[0] ^= GET_U64<bswap>(p + q - U64x(1), 0);
    MathMult::mult64_128(lo5, hi5, state[5], state[0]);

    /*
     * Epilogue.
     */
    i = (state[0] - state[1]) ^ MUSEAIR_CONSTANT[7];
    j = (state[2] - state[3]) ^ MUSEAIR_CONSTANT[8];
    k = (state[4] - state[5]) ^ MUSEAIR_CONSTANT[9];

    int rot = len & 63;

    i = ROTL64(i, rot);
    j = ROTR64(j, rot);
    k = k - len;

    i = i - (lo3 ^ hi3) - (lo4 ^ hi4);
    j = j - (lo5 ^ hi5) - (lo0 ^ hi0);
    k = k - (lo1 ^ hi1) - (lo2 ^ hi2);

    MathMult::mult64_128(lo0, hi0, i, j);
    MathMult::mult64_128(lo1, hi1, j, k);
    MathMult::mult64_128(lo2, hi2, k, i);

    /*
     * BFast state update.
     */
    i = lo2 ^ hi0;
    j = lo0 ^ hi1;
    k = lo1 ^ hi2;

    /*
     * 128-bit finalization.
     */
    MathMult::mult64_128(
            lo3, hi3, i, MUSEAIR_CONSTANT[10]);
    MathMult::mult64_128(
            lo4, hi4, j, MUSEAIR_CONSTANT[11]);
    MathMult::mult64_128(
            lo5, hi5, k, MUSEAIR_CONSTANT[12]);

    *out_lo = lo3 ^ hi4 ^ lo5;
    *out_hi = hi3 ^ lo4 ^ hi5;
}

//------------------------------------------------------------

template <bool bswap>
static void MuseAirSnapRAID(
        const void * in,
        const size_t len,
        const seed_t seed,
        void * out) {

    uint64_t out_lo;
    uint64_t out_hi;

    /*
     * SMHasher3 only supplies one 64-bit seed.
     *
     * MuseAir v2's SMHasher3 adapter uses the same seed for both
     * halves of the B128 seed.
     */
    museair_hash_snapraid<bswap>(
            (const uint8_t *)in,
            len,
            seed,
            seed,
            &out_lo,
            &out_hi);

    out_lo = COND_BSWAP(out_lo, isBE());
    PUT_U64<false>(out_lo, (uint8_t *)out, 0);

    out_hi = COND_BSWAP(out_hi, isBE());
    PUT_U64<false>(out_hi, (uint8_t *)out, 8);
}

//------------------------------------------------------------

REGISTER_FAMILY(museair_snapraid,
   $.src_url    = "https://github.com/amadvance/snapraid/",
   $.src_status = HashFamilyInfo::SRC_ACTIVE
 );

REGISTER_HASH(MuseAir_128__bfast_snapraid,
   $.desc =
         "MuseAir v2, BFast-128, SnapRAID long-path short-input extension",

   $.hash_flags =
         FLAG_HASH_ENDIAN_INDEPENDENT,

   $.impl_flags =
         FLAG_IMPL_MULTIPLY_64_128 |
         FLAG_IMPL_ROTATE_VARIABLE |
         FLAG_IMPL_CANONICAL_LE |
         FLAG_IMPL_LICENSE_PUBLIC_DOMAIN,

   $.bits = 128,

   $.verification_LE = 0x427E5CD7,
   $.verification_BE = 0xED88557C,

   $.hashfn_native = MuseAirSnapRAID<false>,
   $.hashfn_bswap  = MuseAirSnapRAID<true>
 );
