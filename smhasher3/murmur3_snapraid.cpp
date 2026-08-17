/*
 * MurmurHash3 x86_128 SnapRAID adapter for SMHasher3.
 *
 * Based on SnapRAID cmdline/murmur3.c, itself derived from
 * MurmurHash3.cpp revision r136.
 *
 * SnapRAID accepts a full 128-bit seed and initializes the four 32-bit
 * state words independently.
 *
 * SMHasher3 exposes one 64-bit seed_t. As with the other SnapRAID
 * adapters, duplicate that complete 64-bit seed into both halves of the
 * 128-bit SnapRAID seed:
 *
 *     seed[0..7]  = seed
 *     seed[8..15] = seed
 *
 * Therefore, for
 *
 *     seed = (seed_hi << 32) | seed_lo
 *
 * the MurmurHash3 state is initialized as:
 *
 *     h1 = seed_lo
 *     h2 = seed_hi
 *     h3 = seed_lo
 *     h4 = seed_hi
 *
 * This intentionally tests only the diagonal 64-bit subset of the full
 * 128-bit SnapRAID seed space.
 *
 * The SnapRAID source is GPL-3.0-or-later.
 */

#include "Platform.h"
#include "Hashlib.h"

//------------------------------------------------------------------------------

static FORCE_INLINE uint32_t snapraid_murmur3_fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= UINT32_C(0x85ebca6b);
    h ^= h >> 13;
    h *= UINT32_C(0xc2b2ae35);
    h ^= h >> 16;

    return h;
}

//------------------------------------------------------------------------------

template <bool bswap>
static void MurmurHash3_x86_128_SnapRAID(
        const void * in,
        const size_t len,
        const seed_t seed,
        void * out) {

    const uint8_t * data = (const uint8_t *)in;
    const size_t nblocks = len / 16;

    /*
     * Equivalent to a SnapRAID 128-bit seed formed by concatenating two
     * identical copies of the 64-bit SMHasher3 seed.
     */
    const uint32_t seed_lo = (uint32_t)(uint64_t)seed;
    const uint32_t seed_hi = (uint32_t)((uint64_t)seed >> 32);

    uint32_t h1 = seed_lo;
    uint32_t h2 = seed_hi;
    uint32_t h3 = seed_lo;
    uint32_t h4 = seed_hi;

    const uint32_t c1 = UINT32_C(0x239b961b);
    const uint32_t c2 = UINT32_C(0xab0e9789);
    const uint32_t c3 = UINT32_C(0x38b34ae5);
    const uint32_t c4 = UINT32_C(0xa1e38b93);

    // body

    const uint8_t * blocks = data;

    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1 = GET_U32<bswap>(blocks, i * 16 + 0);
        uint32_t k2 = GET_U32<bswap>(blocks, i * 16 + 4);
        uint32_t k3 = GET_U32<bswap>(blocks, i * 16 + 8);
        uint32_t k4 = GET_U32<bswap>(blocks, i * 16 + 12);

        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;
        h1 ^= k1;

        h1 = ROTL32(h1, 19);
        h1 += h2;
        h1 = h1 * 5 + UINT32_C(0x561ccd1b);

        k2 *= c2;
        k2 = ROTL32(k2, 16);
        k2 *= c3;
        h2 ^= k2;

        h2 = ROTL32(h2, 17);
        h2 += h3;
        h2 = h2 * 5 + UINT32_C(0x0bcaa747);

        k3 *= c3;
        k3 = ROTL32(k3, 17);
        k3 *= c4;
        h3 ^= k3;

        h3 = ROTL32(h3, 15);
        h3 += h4;
        h3 = h3 * 5 + UINT32_C(0x96cd1c35);

        k4 *= c4;
        k4 = ROTL32(k4, 18);
        k4 *= c1;
        h4 ^= k4;

        h4 = ROTL32(h4, 13);
        h4 += h1;
        h4 = h4 * 5 + UINT32_C(0x32ac3b17);
    }

    // tail

    const uint8_t * tail = data + nblocks * 16;

    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;

    switch (len & 15) {
    case 15:
        k4 ^= (uint32_t)tail[14] << 16;
        /* FALLTHROUGH */
    case 14:
        k4 ^= (uint32_t)tail[13] << 8;
        /* FALLTHROUGH */
    case 13:
        k4 ^= (uint32_t)tail[12];
        k4 *= c4;
        k4 = ROTL32(k4, 18);
        k4 *= c1;
        h4 ^= k4;
        /* FALLTHROUGH */
    case 12:
        k3 ^= (uint32_t)tail[11] << 24;
        /* FALLTHROUGH */
    case 11:
        k3 ^= (uint32_t)tail[10] << 16;
        /* FALLTHROUGH */
    case 10:
        k3 ^= (uint32_t)tail[9] << 8;
        /* FALLTHROUGH */
    case 9:
        k3 ^= (uint32_t)tail[8];
        k3 *= c3;
        k3 = ROTL32(k3, 17);
        k3 *= c4;
        h3 ^= k3;
        /* FALLTHROUGH */
    case 8:
        k2 ^= (uint32_t)tail[7] << 24;
        /* FALLTHROUGH */
    case 7:
        k2 ^= (uint32_t)tail[6] << 16;
        /* FALLTHROUGH */
    case 6:
        k2 ^= (uint32_t)tail[5] << 8;
        /* FALLTHROUGH */
    case 5:
        k2 ^= (uint32_t)tail[4];
        k2 *= c2;
        k2 = ROTL32(k2, 16);
        k2 *= c3;
        h2 ^= k2;
        /* FALLTHROUGH */
    case 4:
        k1 ^= (uint32_t)tail[3] << 24;
        /* FALLTHROUGH */
    case 3:
        k1 ^= (uint32_t)tail[2] << 16;
        /* FALLTHROUGH */
    case 2:
        k1 ^= (uint32_t)tail[1] << 8;
        /* FALLTHROUGH */
    case 1:
        k1 ^= (uint32_t)tail[0];
        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
    }

    // finalization

    h1 ^= (uint32_t)len;
    h2 ^= (uint32_t)len;
    h3 ^= (uint32_t)len;
    h4 ^= (uint32_t)len;

    h1 += h2;
    h1 += h3;
    h1 += h4;

    h2 += h1;
    h3 += h1;
    h4 += h1;

    h1 = snapraid_murmur3_fmix32(h1);
    h2 = snapraid_murmur3_fmix32(h2);
    h3 = snapraid_murmur3_fmix32(h3);
    h4 = snapraid_murmur3_fmix32(h4);

    h1 += h2;
    h1 += h3;
    h1 += h4;

    h2 += h1;
    h3 += h1;
    h4 += h1;

    PUT_U32<bswap>(h1, (uint8_t *)out, 0);
    PUT_U32<bswap>(h2, (uint8_t *)out, 4);
    PUT_U32<bswap>(h3, (uint8_t *)out, 8);
    PUT_U32<bswap>(h4, (uint8_t *)out, 12);
}

//------------------------------------------------------------------------------

REGISTER_FAMILY(murmur3_snapraid,
   $.src_url    = "https://github.com/amadvance/snapraid/",
   $.src_status = HashFamilyInfo::SRC_ACTIVE
 );

REGISTER_HASH(MurmurHash3_128__snapraid,
   $.desc =
         "MurmurHash3 x86_128 with SnapRAID 128-bit seed interface",

   $.hash_flags =
         0,

   $.impl_flags =
         FLAG_IMPL_MULTIPLY |
         FLAG_IMPL_ROTATE   |
         FLAG_IMPL_LICENSE_GPL3,

   $.bits = 128,

   $.verification_LE = 0x260E7FCF,
   $.verification_BE = 0x37086199,

   $.hashfn_native = MurmurHash3_x86_128_SnapRAID<false>,
   $.hashfn_bswap  = MurmurHash3_x86_128_SnapRAID<true>
 );
