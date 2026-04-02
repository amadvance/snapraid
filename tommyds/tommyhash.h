// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

/** \file
 * Hash functions for the use with ::tommy_hashtable, ::tommy_hashdyn and ::tommy_hashlin.
 */

#ifndef __TOMMYHASH_H
#define __TOMMYHASH_H

#include "tommytypes.h"

/******************************************************************************/
/* hash */

/**
 * Hash function with a 32 bits result.
 * Implementation of the Robert Jenkins "lookup3" hash 32 bits version,
 * from http://www.burtleburtle.net/bob/hash/doobs.html, function hashlittle().
 *
 * This hash is designed to provide a good overall performance on all platforms,
 * including 32 bits. If you target only 64 bits, you can use faster hashes,
 * like SpookyHash or FarmHash.
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the data to hash.
 * \param key_len Size of the data to hash.
 * \note
 * This function is endianness independent.
 * \return The hash value of 32 bits.
 */
TOMMY_API tommy_uint32_t tommy_hash_u32(tommy_uint32_t init_val, const void* void_key, tommy_size_t key_len);

/**
 * Hash function with a 64 bits result.
 * Implementation of the Robert Jenkins "lookup3" hash 64 bits version,
 * from http://www.burtleburtle.net/bob/hash/doobs.html, function hashlittle2().
 *
 * This hash is designed to provide a good overall performance on all platforms,
 * including 32 bits. If you target only 64 bits, you can use faster hashes,
 * like SpookyHash or FarmHash.
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the data to hash.
 * \param key_len Size of the data to hash.
 * \note
 * This function is endianness independent.
 * \return The hash value of 64 bits.
 */
TOMMY_API tommy_uint64_t tommy_hash_u64(tommy_uint64_t init_val, const void* void_key, tommy_size_t key_len);

/**
 * String hash function with a 32 bits result.
 * Implementation is based on Robert Jenkins "lookup3" hash 32 bits version,
 * from http://www.burtleburtle.net/bob/hash/doobs.html, function hashlittle().
 *
 * This hash is designed to handle strings with an unknown length. If you
 * know the string length, the other hash functions are surely faster.
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the string to hash. It has to be 0 terminated.
 * \note
 * This function is endianness independent.
 * \return The hash value of 32 bits.
 */
TOMMY_API tommy_uint32_t tommy_strhash_u32(tommy_uint32_t init_val, const void* void_key);

/**
 * Integer reversible hash function for 32 bits.
 * Implementation of the Robert Jenkins "4-byte Integer Hashing",
 * from http://burtleburtle.net/bob/hash/integer.html
 */
tommy_inline tommy_uint32_t tommy_inthash_u32(tommy_uint32_t key)
{
	key -= key << 6;
	key ^= key >> 17;
	key -= key << 9;
	key ^= key << 4;
	key -= key << 3;
	key ^= key << 10;
	key ^= key >> 15;

	return key;
}

/**
 * Integer reversible hash function for 64 bits.
 * Implementation of the Thomas Wang "Integer Hash Function",
 * from http://web.archive.org/web/20071223173210/http://www.concentric.net/~Ttwang/tech/inthash.htm
 */
tommy_inline tommy_uint64_t tommy_inthash_u64(tommy_uint64_t key)
{
	key = ~key + (key << 21);
	key = key ^ (key >> 24);
	key = key + (key << 3) + (key << 8);
	key = key ^ (key >> 14);
	key = key + (key << 2) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);

	return key;
}

#endif
