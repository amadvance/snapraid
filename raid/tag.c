/*
 * Copyright (C) 2013 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "internal.h"

static struct raid_func {
	const char *name;
	void (*p)();
} RAID_FUNC[] = {
	{ "int8", raid_gen3_int8 },
	{ "int8", raid_gen4_int8 },
	{ "int8", raid_gen5_int8 },
	{ "int8", raid_gen6_int8 },
	{ "int32", raid_gen1_int32 },
	{ "int64", raid_gen1_int64 },
	{ "int32", raid_gen2_int32 },
	{ "int64", raid_gen2_int64 },
	{ "int32", raid_genz_int32 },
	{ "int64", raid_genz_int64 },
	{ "int8", raid_rec1_int8 },
	{ "int8", raid_rec2_int8 },
	{ "int8", raid_recX_int8 },

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	{ "sse2", raid_gen1_sse2 },
	{ "sse2", raid_gen2_sse2 },
	{ "sse2", raid_genz_sse2 },
#endif
#ifdef CONFIG_SSSE3
	{ "ssse3", raid_gen3_ssse3 },
	{ "ssse3", raid_gen4_ssse3 },
	{ "ssse3", raid_gen5_ssse3 },
	{ "ssse3", raid_gen6_ssse3 },
	{ "ssse3", raid_rec1_ssse3 },
	{ "ssse3", raid_rec2_ssse3 },
	{ "ssse3", raid_recX_ssse3 },
#endif
#ifdef CONFIG_AVX2
	{ "avx2", raid_gen1_avx2 },
	{ "avx2", raid_gen2_avx2 },
	{ "avx2", raid_rec1_avx2 },
	{ "avx2", raid_rec2_avx2 },
	{ "avx2", raid_recX_avx2 },
#endif
#endif

#ifdef CONFIG_X86_64
#ifdef CONFIG_SSE2
	{ "sse2e", raid_gen2_sse2ext },
	{ "sse2e", raid_genz_sse2ext },
#endif
#ifdef CONFIG_SSSE3
	{ "ssse3e", raid_gen3_ssse3ext },
	{ "ssse3e", raid_gen4_ssse3ext },
	{ "ssse3e", raid_gen5_ssse3ext },
	{ "ssse3e", raid_gen6_ssse3ext },
#endif
#ifdef CONFIG_AVX2
	{ "avx2e", raid_gen3_avx2ext },
	{ "avx2e", raid_genz_avx2ext },
	{ "avx2e", raid_gen4_avx2ext },
	{ "avx2e", raid_gen5_avx2ext },
	{ "avx2e", raid_gen6_avx2ext },
#endif
#endif
	{ 0, 0 }
};

static const char *raid_tag(void (*func)())
{
	struct raid_func *i = RAID_FUNC;

	while (i->name != 0) {
		if (i->p == func)
			return i->name;
		++i;
	}

	/* LCOV_EXCL_START */
	return "unknown";
	/* LCOV_EXCL_STOP */
}

const char *raid_gen1_tag(void)
{
	return raid_tag(raid_gen_ptr[0]);
}

const char *raid_gen2_tag(void)
{
	return raid_tag(raid_gen_ptr[1]);
}

const char *raid_genz_tag(void)
{
	return raid_tag(raid_genz_ptr);
}

const char *raid_gen3_tag(void)
{
	return raid_tag(raid_gen_ptr[2]);
}

const char *raid_gen4_tag(void)
{
	return raid_tag(raid_gen_ptr[3]);
}

const char *raid_gen5_tag(void)
{
	return raid_tag(raid_gen_ptr[4]);
}

const char *raid_gen6_tag(void)
{
	return raid_tag(raid_gen_ptr[5]);
}

const char *raid_rec1_tag(void)
{
	return raid_tag(raid_rec_ptr[0]);
}

const char *raid_rec2_tag(void)
{
	return raid_tag(raid_rec_ptr[1]);
}

const char *raid_recX_tag(void)
{
	return raid_tag(raid_rec_ptr[2]);
}

