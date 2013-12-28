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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "internal.h"
#include "cpu.h"

void raid_init(void)
{
	raid_par3_ptr = raid_par3_int8;
	raid_par_ptr[3] = raid_par4_int8;
	raid_par_ptr[4] = raid_par5_int8;
	raid_par_ptr[5] = raid_par6_int8;

	if (sizeof(void *) == 4) {
		raid_par_ptr[0] = raid_par1_int32;
		raid_par_ptr[1] = raid_par2_int32;
		raid_parz_ptr = raid_parz_int32;
	} else {
		raid_par_ptr[0] = raid_par1_int64;
		raid_par_ptr[1] = raid_par2_int64;
		raid_parz_ptr = raid_parz_int64;
	}

	raid_rec_ptr[0] = raid_rec1_int8;
	raid_rec_ptr[1] = raid_rec2_int8;
	raid_rec_ptr[2] = raid_recX_int8;
	raid_rec_ptr[3] = raid_recX_int8;
	raid_rec_ptr[4] = raid_recX_int8;
	raid_rec_ptr[5] = raid_recX_int8;

#if defined(__i386__) || defined(__x86_64__)
	if (raid_cpu_has_sse2()) {
		raid_par_ptr[0] = raid_par1_sse2;
#if defined(__x86_64__)
		if (raid_cpu_has_slowextendedreg()) {
			raid_par_ptr[1] = raid_par2_sse2;
			raid_parz_ptr = raid_parz_sse2;
		} else {
			raid_par_ptr[1] = raid_par2_sse2ext;
			raid_parz_ptr = raid_parz_sse2ext;
		}
#else
		raid_par_ptr[1] = raid_par2_sse2;
		raid_parz_ptr = raid_parz_sse2;
#endif
	}

	if (raid_cpu_has_ssse3()) {
#if defined(__x86_64__)
		raid_par3_ptr = raid_par3_ssse3ext;
		raid_par_ptr[3] = raid_par4_ssse3ext;
		raid_par_ptr[4] = raid_par5_ssse3ext;
		raid_par_ptr[5] = raid_par6_ssse3ext;
#else
		raid_par3_ptr = raid_par3_ssse3;
		raid_par_ptr[3] = raid_par4_ssse3;
		raid_par_ptr[4] = raid_par5_ssse3;
		raid_par_ptr[5] = raid_par6_ssse3;
#endif
		raid_rec_ptr[0] = raid_rec1_ssse3;
		raid_rec_ptr[1] = raid_rec2_ssse3;
		raid_rec_ptr[2] = raid_recX_ssse3;
		raid_rec_ptr[3] = raid_recX_ssse3;
		raid_rec_ptr[4] = raid_recX_ssse3;
		raid_rec_ptr[5] = raid_recX_ssse3;
	}
#endif

	/* set the default mode */
	raid_mode(RAID_MODE_CAUCHY);
}

static struct raid_func {
	const char *name;
	void *p;
} RAID_FUNC[] = {
	{ "int8", raid_par3_int8 },
	{ "int8", raid_par4_int8 },
	{ "int8", raid_par5_int8 },
	{ "int8", raid_par6_int8 },
	{ "int32", raid_par1_int32 },
	{ "int64", raid_par1_int64 },
	{ "int32", raid_par2_int32 },
	{ "int64", raid_par2_int64 },
	{ "int32", raid_parz_int32 },
	{ "int64", raid_parz_int64 },
	{ "int8", raid_rec1_int8 },
	{ "int8", raid_rec2_int8 },
	{ "int8", raid_recX_int8 },

#if defined(__i386__) || defined(__x86_64__)
	{ "sse2", raid_par1_sse2 },
	{ "sse2", raid_par2_sse2 },
	{ "sse2", raid_parz_sse2 },
	{ "ssse3", raid_par3_ssse3 },
	{ "ssse3", raid_par4_ssse3 },
	{ "ssse3", raid_par5_ssse3 },
	{ "ssse3", raid_par6_ssse3 },
	{ "ssse3", raid_rec1_ssse3 },
	{ "ssse3", raid_rec2_ssse3 },
	{ "ssse3", raid_recX_ssse3 },
#endif

#if defined(__x86_64__)
	{ "sse2e", raid_par2_sse2ext },
	{ "sse2e", raid_parz_sse2ext },
	{ "ssse3e", raid_par3_ssse3ext },
	{ "ssse3e", raid_par4_ssse3ext },
	{ "ssse3e", raid_par5_ssse3ext },
	{ "ssse3e", raid_par6_ssse3ext },
#endif
	{ 0, 0 }
};

static const char *raid_tag(void *func)
{
	struct raid_func *i = RAID_FUNC;
	while (i->name != 0) {
		if (i->p == func)
			return i->name;
		++i;
	}
	return "unknown";
}

const char *raid_par1_tag(void)
{
	return raid_tag(raid_par_ptr[0]);
}

const char *raid_par2_tag(void)
{
	return raid_tag(raid_par_ptr[1]);
}

const char *raid_parz_tag(void)
{
	return raid_tag(raid_parz_ptr);
}

const char *raid_par3_tag(void)
{
	return raid_tag(raid_par_ptr[2]);
}

const char *raid_par4_tag(void)
{
	return raid_tag(raid_par_ptr[3]);
}

const char *raid_par5_tag(void)
{
	return raid_tag(raid_par_ptr[4]);
}

const char *raid_par6_tag(void)
{
	return raid_tag(raid_par_ptr[5]);
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

