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
 * Implementation of MurmurHash3 (x86_32) by Austin Appleby,
 * from https://github.com/aappleby/smhasher
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the data to hash.
 * \param key_len Size of the data to hash.
 * \note
 * This function is endianness independent and alignment independent.
 * It produces identical hash results to ::tommy_strhash_u32 for 0-terminated strings.
 * Compatibility across different versions of the library is not guaranteed:
 * this hash is intended for in-memory runtime use only and not for persistent storage.
 * It is not state-of-the-art against malicious collision attacks, but it is simple and fast.
 * \return The hash value of 32 bits.
 */
TOMMY_API tommy_uint32_t tommy_hash_u32(tommy_uint32_t init_val, const void* void_key, tommy_size_t key_len);

/**
 * Hash function with a 64 bits result.
 * Implementation of MurmurHash3 (x64_64) by Austin Appleby,
 * from https://github.com/aappleby/smhasher
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the data to hash.
 * \param key_len Size of the data to hash.
 * \note
 * This function is endianness independent and alignment independent.
 * It produces identical hash results to ::tommy_strhash_u64 for 0-terminated strings.
 * Compatibility across different versions of the library is not guaranteed:
 * this hash is intended for in-memory runtime use only and not for persistent storage.
 * It is not state-of-the-art against malicious collision attacks, but it is simple and fast.
 * \return The hash value of 64 bits.
 */
TOMMY_API tommy_uint64_t tommy_hash_u64(tommy_uint64_t init_val, const void* void_key, tommy_size_t key_len);

/**
 * String hash function with a 32 bits result.
 * Implementation is based on MurmurHash3 (x86_32) by Austin Appleby,
 * from https://github.com/aappleby/smhasher
 *
 * This hash is designed to handle strings with an unknown length.
 * If the string is aligned, it operates in a single pass without calling strlen().
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the string to hash. It has to be 0 terminated.
 * \note
 * This function is endianness independent and alignment independent.
 * Compatibility across different versions of the library is not guaranteed:
 * this hash is intended for in-memory runtime use only and not for persistent storage.
 * It is not state-of-the-art against malicious collision attacks, but it is simple and fast.
 * \return The hash value of 32 bits.
 */
TOMMY_API tommy_uint32_t tommy_strhash_u32(tommy_uint32_t init_val, const void* void_key);

/**
 * String hash function with a 64 bits result.
 * Implementation is based on MurmurHash3 (x64_64) by Austin Appleby,
 * from https://github.com/aappleby/smhasher
 *
 * This hash is designed to handle strings with an unknown length.
 * If the string is aligned, it operates in a single pass without calling strlen().
 *
 * \param init_val Initialization value.
 * Using a different initialization value, you can generate a completely different set of hash values.
 * Use 0 if not relevant.
 * \param void_key Pointer to the string to hash. It has to be 0 terminated.
 * \note
 * This function is endianness independent and alignment independent.
 * Compatibility across different versions of the library is not guaranteed:
 * this hash is intended for in-memory runtime use only and not for persistent storage.
 * It is not state-of-the-art against malicious collision attacks, but it is simple and fast.
 * \return The hash value of 64 bits.
 */
TOMMY_API tommy_uint64_t tommy_strhash_u64(tommy_uint64_t init_val, const void* void_key);

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

