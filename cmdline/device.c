/*
 * Copyright (C) 2015 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "portable.h"

#include "support.h"
#include "state.h"
#include "raid/raid.h"

void device_name_set(devinfo_t* dev, const char* name, int index)
{
	if (index == 0)
		pathcpy(dev->name, sizeof(dev->name), name);
	else
		pathprint(dev->name, sizeof(dev->name), "%s/%d", name, index);
}

/**
 * The following are Failure Rate tables computed from the data that
 * BackBlaze made available at:
 *
 * Reliability Data Set For 41,000 Hard Drives Now Open Source
 * https://www.backblaze.com/blog/hard-drive-data-feb2015/
 *
 * Hard Drive Data Sets
 * https://www.backblaze.com/hard-drive-test-data.html
 *
 * Note that in this data:
 *  - Disks all passed the load-testing and have made it to production,
 *    and then Dead On Arrival (DOA) failures are excluded.
 *  - Disks that are predicted to fail by BackBlaze are removed before
 *    they really fail, not counting as a failure.
 *
 * The following tables are computed using the data from 2014-02-14 to 2014-12-31
 * because it's the period when more SMART attributes were gathered.
 *
 * In this period there are 47322 disk seen, with 1988 removed because failed,
 * and with 4121 removed because predicted to fail.
 */

/**
 * Number of data point in each table.
 */
#define SMART_MEASURES 256

/*
 * Divider for SMART attribute 5
 */
static unsigned SMART_5_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 5 at a given value.
 */
static double SMART_5_R[SMART_MEASURES] = {
	0.0026, 0.0748, 0.0919, 0.1013, 0.1079,
	0.1137, 0.1194, 0.1235, 0.1301, 0.1398,
	0.1453, 0.1490, 0.1528, 0.1566, 0.1595,
	0.1635, 0.1656, 0.1701, 0.1718, 0.1740,
	0.1762, 0.1787, 0.1808, 0.1833, 0.1858,
	0.1885, 0.1901, 0.1915, 0.1934, 0.1958,
	0.1975, 0.1993, 0.2014, 0.2048, 0.2068,
	0.2088, 0.2109, 0.2120, 0.2137, 0.2160,
	0.2173, 0.2214, 0.2226, 0.2237, 0.2262,
	0.2277, 0.2292, 0.2304, 0.2338, 0.2369,
	0.2381, 0.2396, 0.2411, 0.2427, 0.2445,
	0.2462, 0.2472, 0.2488, 0.2496, 0.2504,
	0.2514, 0.2525, 0.2535, 0.2544, 0.2554,
	0.2571, 0.2583, 0.2601, 0.2622, 0.2631,
	0.2635, 0.2644, 0.2659, 0.2675, 0.2682,
	0.2692, 0.2701, 0.2707, 0.2712, 0.2726,
	0.2745, 0.2767, 0.2778, 0.2784, 0.2800,
	0.2814, 0.2834, 0.2839, 0.2851, 0.2877,
	0.2883, 0.2891, 0.2900, 0.2907, 0.2916,
	0.2934, 0.2950, 0.2969, 0.2975, 0.2983,
	0.2999, 0.3006, 0.3013, 0.3021, 0.3033,
	0.3054, 0.3066, 0.3074, 0.3082, 0.3094,
	0.3106, 0.3112, 0.3120, 0.3137, 0.3141,
	0.3145, 0.3151, 0.3159, 0.3169, 0.3174,
	0.3181, 0.3194, 0.3215, 0.3219, 0.3231,
	0.3234, 0.3237, 0.3242, 0.3255, 0.3270,
	0.3283, 0.3286, 0.3289, 0.3304, 0.3315,
	0.3322, 0.3347, 0.3361, 0.3382, 0.3384,
	0.3395, 0.3398, 0.3401, 0.3405, 0.3411,
	0.3431, 0.3435, 0.3442, 0.3447, 0.3450,
	0.3455, 0.3464, 0.3472, 0.3486, 0.3497,
	0.3501, 0.3509, 0.3517, 0.3531, 0.3535,
	0.3540, 0.3565, 0.3569, 0.3576, 0.3579,
	0.3584, 0.3590, 0.3594, 0.3599, 0.3621,
	0.3627, 0.3642, 0.3649, 0.3655, 0.3658,
	0.3667, 0.3683, 0.3699, 0.3704, 0.3707,
	0.3711, 0.3715, 0.3718, 0.3721, 0.3727,
	0.3740, 0.3744, 0.3748, 0.3753, 0.3756,
	0.3761, 0.3766, 0.3775, 0.3794, 0.3801,
	0.3804, 0.3813, 0.3817, 0.3823, 0.3831,
	0.3847, 0.3875, 0.3881, 0.3886, 0.3890,
	0.3893, 0.3896, 0.3900, 0.3907, 0.3923,
	0.3925, 0.3933, 0.3936, 0.3961, 0.3971,
	0.3981, 0.3989, 0.4007, 0.4012, 0.4018,
	0.4023, 0.4027, 0.4041, 0.4048, 0.4056,
	0.4073, 0.4079, 0.4086, 0.4104, 0.4107,
	0.4109, 0.4112, 0.4118, 0.4133, 0.4139,
	0.4144, 0.4146, 0.4148, 0.4164, 0.4165,
	0.4174, 0.4191, 0.4197, 0.4201, 0.4204,
	0.4210, 0.4213, 0.4216, 0.4221, 0.4231,
	0.4235, 0.4237, 0.4239, 0.4241, 0.4244,
	0.4249,
};

/*
 * Divider for SMART attribute 187
 */
static unsigned SMART_187_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 187 at a given value.
 */
static double SMART_187_R[SMART_MEASURES] = {
	0.0039, 0.1287, 0.1579, 0.1776, 0.1905,
	0.2013, 0.2226, 0.3263, 0.3612, 0.3869,
	0.4086, 0.4292, 0.4559, 0.5278, 0.5593,
	0.5847, 0.6124, 0.6345, 0.6517, 0.6995,
	0.7308, 0.7541, 0.7814, 0.8122, 0.8306,
	0.8839, 0.9100, 0.9505, 0.9906, 1.0254,
	1.0483, 1.1060, 1.1280, 1.1624, 1.1895,
	1.2138, 1.2452, 1.2864, 1.3120, 1.3369,
	1.3705, 1.3894, 1.4055, 1.4218, 1.4434,
	1.4670, 1.4834, 1.4993, 1.5174, 1.5400,
	1.5572, 1.5689, 1.5808, 1.6198, 1.6346,
	1.6405, 1.6570, 1.6618, 1.6755, 1.6877,
	1.7100, 1.7258, 1.7347, 1.7814, 1.7992,
	1.8126, 1.8225, 1.8269, 1.8341, 1.8463,
	1.8765, 1.8850, 1.9005, 1.9281, 1.9398,
	1.9618, 1.9702, 1.9905, 2.0099, 2.0480,
	2.0565, 2.0611, 2.0709, 2.0846, 2.0895,
	2.0958, 2.1008, 2.1055, 2.1097, 2.1235,
	2.1564, 2.1737, 2.1956, 2.1989, 2.2015,
	2.2148, 2.2355, 2.2769, 2.2940, 2.3045,
	2.3096, 2.3139, 2.3344, 2.3669, 2.3779,
	2.3941, 2.4036, 2.4396, 2.4473, 2.4525,
	2.4656, 2.4762, 2.4787, 2.5672, 2.5732,
	2.5755, 2.5794, 2.5886, 2.6100, 2.6144,
	2.6341, 2.6614, 2.6679, 2.6796, 2.6847,
	2.6872, 2.6910, 2.6934, 2.6995, 2.7110,
	2.7179, 2.7204, 2.7232, 2.7282, 2.7355,
	2.7375, 2.7422, 2.7558, 2.7580, 2.7643,
	2.7767, 2.7770, 2.8016, 2.9292, 2.9294,
	2.9337, 2.9364, 2.9409, 2.9436, 2.9457,
	2.9466, 2.9498, 2.9543, 2.9570, 2.9573,
	2.9663, 2.9708, 2.9833, 2.9859, 2.9895,
	2.9907, 2.9932, 2.9935, 3.0021, 3.0035,
	3.0079, 3.0103, 3.0126, 3.0151, 3.0266,
	3.0288, 3.0320, 3.0330, 3.0343, 3.0373,
	3.0387, 3.0438, 3.0570, 3.0579, 3.0616,
	3.0655, 3.0728, 3.0771, 3.0794, 3.0799,
	3.0812, 3.1769, 3.1805, 3.1819, 3.1860,
	3.1869, 3.2004, 3.2016, 3.2025, 3.2070,
	3.2129, 3.2173, 3.2205, 3.2254, 3.2263,
	3.2300, 3.2413, 3.2543, 3.2580, 3.2595,
	3.2611, 3.2624, 3.2787, 3.2798, 3.2809,
	3.2823, 3.2833, 3.2834, 3.2853, 3.2866,
	3.3332, 3.3580, 3.3595, 3.3625, 3.3631,
	3.3667, 3.3702, 3.3737, 3.3742, 3.3747,
	3.3769, 3.3775, 3.3791, 3.3809, 3.3813,
	3.3814, 3.3822, 3.3827, 3.3828, 3.3833,
	3.3833, 3.3843, 3.3882, 3.3963, 3.4047,
	3.4057, 3.4213, 3.4218, 3.4230, 3.4231,
	3.4240, 3.4262, 3.4283, 3.4283, 3.4288,
	3.4293, 3.4302, 3.4317, 3.4478, 3.4486,
	3.4520,
};

/*
 * Divider for SMART attribute 188
 */
static unsigned SMART_188_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 188 at a given value.
 */
static double SMART_188_R[SMART_MEASURES] = {
	0.0025, 0.0129, 0.0182, 0.0215, 0.0236,
	0.0257, 0.0279, 0.0308, 0.0341, 0.0382,
	0.0430, 0.0491, 0.0565, 0.0658, 0.0770,
	0.0906, 0.1037, 0.1197, 0.1355, 0.1525,
	0.1686, 0.1864, 0.2011, 0.2157, 0.2281,
	0.2404, 0.2505, 0.2591, 0.2676, 0.2766,
	0.2827, 0.2913, 0.2999, 0.3100, 0.3185,
	0.3298, 0.3361, 0.3446, 0.3506, 0.3665,
	0.3699, 0.3820, 0.3890, 0.4059, 0.4108,
	0.4255, 0.4290, 0.4424, 0.4473, 0.4617,
	0.4667, 0.4770, 0.4829, 0.4977, 0.4997,
	0.5102, 0.5137, 0.5283, 0.5316, 0.5428,
	0.5480, 0.5597, 0.5634, 0.5791, 0.5826,
	0.5929, 0.5945, 0.6025, 0.6102, 0.6175,
	0.6245, 0.6313, 0.6421, 0.6468, 0.6497,
	0.6557, 0.6570, 0.6647, 0.6698, 0.6769,
	0.6849, 0.6884, 0.6925, 0.7025, 0.7073,
	0.7161, 0.7223, 0.7256, 0.7280, 0.7411,
	0.7445, 0.7530, 0.7628, 0.7755, 0.7900,
	0.8006, 0.8050, 0.8098, 0.8132, 0.8192,
	0.8230, 0.8293, 0.8356, 0.8440, 0.8491,
	0.8672, 0.8766, 0.8907, 0.8934, 0.8992,
	0.9062, 0.9111, 0.9209, 0.9290, 0.9329,
	0.9378, 0.9385, 0.9402, 0.9427, 0.9448,
	0.9459, 0.9568, 0.9626, 0.9628, 0.9730,
	0.9765, 0.9797, 0.9825, 0.9873, 0.9902,
	0.9926, 0.9991, 1.0031, 1.0044, 1.0062,
	1.0120, 1.0148, 1.0188, 1.0218, 1.0231,
	1.0249, 1.0277, 1.0335, 1.0355, 1.0417,
	1.0467, 1.0474, 1.0510, 1.0529, 1.0532,
	1.0562, 1.0610, 1.0702, 1.0708, 1.0800,
	1.0804, 1.0845, 1.1120, 1.1191, 1.1225,
	1.1264, 1.1265, 1.1335, 1.1347, 1.1479,
	1.1479, 1.1519, 1.1545, 1.1645, 1.1646,
	1.1647, 1.1649, 1.1678, 1.1713, 1.1723,
	1.1733, 1.1736, 1.1736, 1.1738, 1.1739,
	1.1739, 1.1741, 1.1741, 1.1746, 1.1746,
	1.1748, 1.1750, 1.1760, 1.1794, 1.1854,
	1.1908, 1.1912, 1.1912, 1.1971, 1.2033,
	1.2033, 1.2120, 1.2166, 1.2185, 1.2185,
	1.2189, 1.2211, 1.2226, 1.2234, 1.2320,
	1.2345, 1.2345, 1.2347, 1.2350, 1.2350,
	1.2407, 1.2408, 1.2408, 1.2408, 1.2409,
	1.2460, 1.2518, 1.2519, 1.2519, 1.2519,
	1.2520, 1.2520, 1.2521, 1.2521, 1.2521,
	1.2593, 1.2745, 1.2760, 1.2772, 1.2831,
	1.2833, 1.2890, 1.2906, 1.3166, 1.3201,
	1.3202, 1.3202, 1.3202, 1.3204, 1.3204,
	1.3314, 1.3422, 1.3423, 1.3441, 1.3491,
	1.3583, 1.3602, 1.3606, 1.3636, 1.3650,
	1.3661, 1.3703, 1.3708, 1.3716, 1.3730,
	1.3731,
};

/*
 * Divider for SMART attribute 197
 */
static unsigned SMART_197_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 197 at a given value.
 */
static double SMART_197_R[SMART_MEASURES] = {
	0.0028, 0.2972, 0.3883, 0.4363, 0.4644,
	0.4813, 0.4948, 0.5051, 0.5499, 0.8535,
	0.8678, 0.8767, 0.8882, 0.8933, 0.9012,
	0.9076, 0.9368, 1.1946, 1.2000, 1.2110,
	1.2177, 1.2305, 1.2385, 1.2447, 1.2699,
	1.4713, 1.4771, 1.4802, 1.4887, 1.5292,
	1.5384, 1.5442, 1.5645, 1.7700, 1.7755,
	1.7778, 1.7899, 1.7912, 1.7991, 1.7998,
	1.8090, 1.9974, 1.9992, 2.0088, 2.0132,
	2.0146, 2.0161, 2.0171, 2.0273, 2.1845,
	2.1866, 2.1877, 2.1900, 2.1922, 2.1944,
	2.1974, 2.2091, 2.3432, 2.3459, 2.3463,
	2.3468, 2.3496, 2.3503, 2.3533, 2.3593,
	2.4604, 2.4606, 2.4609, 2.4612, 2.4620,
	2.4626, 2.4638, 2.4689, 2.5575, 2.5581,
	2.5586, 2.5586, 2.5588, 2.5602, 2.5602,
	2.5648, 2.6769, 2.6769, 2.6769, 2.6794,
	2.6805, 2.6811, 2.6814, 2.6862, 2.7742,
	2.7755, 2.7771, 2.7780, 2.7790, 2.7797,
	2.7807, 2.7871, 2.9466, 2.9478, 2.9492,
	2.9612, 2.9618, 2.9624, 2.9628, 2.9669,
	3.1467, 3.1481, 3.1494, 3.1499, 3.1504,
	3.1507, 3.1509, 3.1532, 3.2675, 3.2681,
	3.2703, 3.2712, 3.2714, 3.2726, 3.2726,
	3.2743, 3.3376, 3.3379, 3.3382, 3.3397,
	3.3403, 3.3410, 3.3410, 3.3429, 3.4052,
	3.4052, 3.4052, 3.4052, 3.4052, 3.4053,
	3.4053, 3.4075, 3.4616, 3.4616, 3.4616,
	3.4616, 3.4616, 3.4616, 3.4620, 3.4634,
	3.4975, 3.4975, 3.4975, 3.4975, 3.4979,
	3.4979, 3.4979, 3.4998, 3.5489, 3.5489,
	3.5489, 3.5489, 3.5489, 3.5493, 3.5497,
	3.5512, 3.5827, 3.5828, 3.5828, 3.5828,
	3.5828, 3.5828, 3.5828, 3.5844, 3.6251,
	3.6251, 3.6251, 3.6267, 3.6267, 3.6271,
	3.6271, 3.6279, 3.6562, 3.6562, 3.6563,
	3.7206, 3.7242, 3.7332, 3.7332, 3.7346,
	3.7548, 3.7548, 3.7553, 3.7576, 3.7581,
	3.7586, 3.7587, 3.7600, 3.7773, 3.7812,
	3.7836, 3.7841, 3.7842, 3.7851, 3.7856,
	3.7876, 3.8890, 3.8890, 3.8890, 3.8890,
	3.8890, 3.8890, 3.8890, 3.8897, 3.9111,
	3.9114, 3.9114, 3.9114, 3.9114, 3.9114,
	3.9114, 3.9126, 3.9440, 3.9440, 3.9440,
	3.9440, 3.9440, 3.9498, 3.9498, 3.9509,
	3.9783, 3.9783, 3.9784, 3.9784, 3.9784,
	3.9784, 4.0012, 4.0019, 4.0406, 4.0413,
	4.0413, 4.0413, 4.0413, 4.0414, 4.0414,
	4.0421, 4.0552, 4.0552, 4.0558, 4.0558,
	4.0558, 4.0558, 4.0558, 4.0563, 4.0753,
	4.0753, 4.0760, 4.1131, 4.1131, 4.1131,
	4.1131,
};

/*
 * Divider for SMART attribute 198
 */
static unsigned SMART_198_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 198 at a given value.
 */
static double SMART_198_R[SMART_MEASURES] = {
	0.0030, 0.5479, 0.5807, 0.5949, 0.6046,
	0.6086, 0.6139, 0.6224, 0.6639, 1.0308,
	1.0329, 1.0364, 1.0371, 1.0387, 1.0399,
	1.0421, 1.0675, 1.3730, 1.3733, 1.3741,
	1.3741, 1.3752, 1.3794, 1.3800, 1.3985,
	1.6291, 1.6303, 1.6309, 1.6352, 1.6384,
	1.6448, 1.6464, 1.6645, 1.8949, 1.8951,
	1.8962, 1.9073, 1.9073, 1.9152, 1.9161,
	1.9240, 2.1308, 2.1315, 2.1328, 2.1328,
	2.1328, 2.1328, 2.1329, 2.1439, 2.3203,
	2.3205, 2.3205, 2.3205, 2.3205, 2.3205,
	2.3205, 2.3265, 2.4729, 2.4729, 2.4729,
	2.4729, 2.4729, 2.4729, 2.4729, 2.4778,
	2.5900, 2.5900, 2.5901, 2.5901, 2.5901,
	2.5901, 2.5901, 2.5949, 2.6964, 2.6965,
	2.6965, 2.6965, 2.6965, 2.6965, 2.6965,
	2.7010, 2.8328, 2.8328, 2.8328, 2.8329,
	2.8329, 2.8329, 2.8329, 2.8366, 2.9405,
	2.9405, 2.9405, 2.9405, 2.9405, 2.9405,
	2.9405, 2.9442, 3.1344, 3.1344, 3.1346,
	3.1463, 3.1463, 3.1463, 3.1463, 3.1493,
	3.3076, 3.3076, 3.3076, 3.3076, 3.3076,
	3.3077, 3.3077, 3.3097, 3.4456, 3.4456,
	3.4456, 3.4456, 3.4456, 3.4456, 3.4456,
	3.4473, 3.5236, 3.5236, 3.5236, 3.5236,
	3.5236, 3.5236, 3.5236, 3.5249, 3.6004,
	3.6004, 3.6004, 3.6004, 3.6004, 3.6004,
	3.6004, 3.6026, 3.6684, 3.6684, 3.6684,
	3.6684, 3.6684, 3.6684, 3.6684, 3.6697,
	3.7121, 3.7121, 3.7121, 3.7121, 3.7121,
	3.7121, 3.7121, 3.7136, 3.7744, 3.7744,
	3.7744, 3.7744, 3.7744, 3.7745, 3.7745,
	3.7756, 3.8151, 3.8151, 3.8151, 3.8151,
	3.8151, 3.8151, 3.8151, 3.8163, 3.8673,
	3.8673, 3.8673, 3.8673, 3.8673, 3.8673,
	3.8673, 3.8680, 3.9044, 3.9044, 3.9044,
	3.9044, 3.9044, 3.9044, 3.9044, 3.9056,
	3.9297, 3.9297, 3.9297, 3.9297, 3.9297,
	3.9297, 3.9297, 3.9305, 3.9494, 3.9494,
	3.9494, 3.9494, 3.9494, 3.9494, 3.9494,
	3.9514, 4.0725, 4.0725, 4.0725, 4.0725,
	4.0725, 4.0725, 4.0725, 4.0731, 4.0990,
	4.0993, 4.0993, 4.0993, 4.0993, 4.0993,
	4.0993, 4.1004, 4.1385, 4.1385, 4.1385,
	4.1386, 4.1386, 4.1387, 4.1387, 4.1398,
	4.1732, 4.2284, 4.2284, 4.2284, 4.2284,
	4.2284, 4.2284, 4.2290, 4.2781, 4.2781,
	4.2963, 4.2963, 4.2963, 4.2963, 4.2963,
	4.2971, 4.3141, 4.3141, 4.3141, 4.3141,
	4.3141, 4.3141, 4.3141, 4.3146, 4.3393,
	4.3393, 4.3393, 4.3393, 4.3393, 4.3393,
	4.3393,
};

/**
 * Computes the estimated Annual Failure Rate from the specified table.
 */
static double smart_afr_value(double* tab, unsigned step, uint64_t value)
{
	value /= step;

	if (value >= SMART_MEASURES)
		value = SMART_MEASURES - 1;

	/* table rates are for a month, so we scale to a year */
	return 365.0 / 30.0 * tab[value];
}

/**
 * Computes the estimated Annual Failure Rate of a set of SMART attributes.
 *
 * We define the Annual Failure Rate as the average number of
 * failures you expect in a year from a disk slot:
 *
 *   AFR = 8760/MTBF (Mean Time Between Failures in hours).
 *
 * Note that this definition is different from the one given
 * by Seagate, that defines AFR = 1 - exp(-8760/MTBF), that
 * instead represents the probability of a failure in the next
 * year.
 *
 * To combine the different AFR from different SMART attributes,
 * we use the maximum rate reported, and we do not sum them,
 * because the attributes are not independent.
 */
static double smart_afr(uint64_t* smart, const char* model)
{
	double afr = 0;
	uint64_t mask32 = 0xffffffffU;
	uint64_t mask16 = 0xffffU;

	if (smart[5] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_5_R, SMART_5_STEP, smart[5] & mask32);
		if (afr < r)
			afr = r;
	}

	if (smart[187] != SMART_UNASSIGNED) {
		/* with some disks, only the lower 16 bits are significative */
		/* See: http://web.archive.org/web/20130507072056/http://media.kingston.com/support/downloads/MKP_306_SMART_attribute.pdf */
		double r = smart_afr_value(SMART_187_R, SMART_187_STEP, smart[187] & mask16);
		if (afr < r)
			afr = r;
	}

	if (
		/**
		 * Don't check Command_Timeout (188) for Seagate disks.
		 *
		 * It's reported by users that for Archive SMR (Shingled Magnetic Recording)
		 * and IronWolf disks to be a not significative test as
		 * this value increases too often also on sane disks.
		 */
		strncmp(model, "ST", 2) != 0 && smart[188] != SMART_UNASSIGNED
	) {
		/* with Seagate disks, there are three different 16 bits value reported */
		/* the lowest one is the most significant */
		double r = smart_afr_value(SMART_188_R, SMART_188_STEP, smart[188] & mask16);
		if (afr < r)
			afr = r;
	}

	if (smart[197] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_197_R, SMART_197_STEP, smart[197] & mask32);
		if (afr < r)
			afr = r;
	}

	if (smart[198] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_198_R, SMART_198_STEP, smart[198] & mask32);
		if (afr < r)
			afr = r;
	}

	return afr;
}

/**
 * Factorial.
 */
static double fact(unsigned n)
{
	double v = 1;

	while (n > 1)
		v *= n--;

	return v;
}

/**
 * Probability of having exactly ::n events in a Poisson
 * distribution with rate ::rate in a time unit.
 */
static double poisson_prob_n_failures(double rate, unsigned n)
{
	return pow(rate, n) * exp(-rate) / fact(n);
}

/**
 * Probability of having ::n or more events in a Poisson
 * distribution with rate ::rate in a time unit.
 */
static double poisson_prob_n_or_more_failures(double rate, unsigned n)
{
	double p_neg = 0;
	unsigned i;

	for (i = 0; i < n; ++i)
		p_neg += poisson_prob_n_failures(rate, n - 1 - i);

	return 1 - p_neg;
}

/**
 * Probability of having data loss in a RAID system with the specified ::redundancy
 * supposing the specified ::array_failure_rate, and ::replace_rate.
 */
static double raid_prob_of_one_or_more_failures(double array_failure_rate, double replace_rate, unsigned n, unsigned redundancy)
{
	unsigned i;
	double MTBF;
	double MTTR;
	double MTTDL;
	double raid_failure_rate;

	/*
	 * Use the MTTDL model (Mean Time To Data Loss) to estimate the
	 * failure rate of the array.
	 *
	 * See:
	 * Garth Alan Gibson, "Redundant Disk Arrays: Reliable, Parallel Secondary Storage", 1990
	 */

	/* avoid division by zero */
	if (array_failure_rate == 0)
		return 0;

	/* get the Mean Time Between Failure of a single disk */
	/* from the array failure rate */
	MTBF = n / array_failure_rate;

	/* get the Mean Time Between Repair (the time that a failed disk is replaced) */
	/* from the repair rate */
	MTTR = 1.0 / replace_rate;

	/* use the approximated MTTDL equation */
	MTTDL = pow(MTBF, redundancy + 1) / pow(MTTR, redundancy);
	for (i = 0; i < redundancy + 1; ++i)
		MTTDL /= n - i;

	/* the raid failure rate is just the inverse of the MTTDL */
	raid_failure_rate = 1.0 / MTTDL;

	/* probability of at least one RAID failure */
	/* note that is almost equal at the probability of */
	/* the first failure. */
	return poisson_prob_n_or_more_failures(raid_failure_rate, 1);
}

static void state_info_log(devinfo_t* devinfo)
{
	char esc_buffer[ESC_MAX];

	log_tag("info:%s:%s\n", devinfo->file, devinfo->name);
	if (devinfo->serial[0])
		log_tag("attr:%s:%s:serial:%s\n", devinfo->file, devinfo->name, esc_tag(devinfo->serial, esc_buffer));
	if (devinfo->vendor[0])
		log_tag("attr:%s:%s:vendor:%s\n", devinfo->file, devinfo->name, esc_tag(devinfo->vendor, esc_buffer));
	if (devinfo->model[0])
		log_tag("attr:%s:%s:model:%s\n", devinfo->file, devinfo->name, esc_tag(devinfo->model, esc_buffer));
	if (devinfo->info[INFO_SIZE] != SMART_UNASSIGNED)
		log_tag("attr:%s:%s:size:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->info[INFO_SIZE]);
	if (devinfo->info[INFO_ROTATION_RATE] != SMART_UNASSIGNED)
		log_tag("attr:%s:%s:rotationrate:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->info[INFO_ROTATION_RATE]);
}

static void state_smart_log(devinfo_t* devinfo, double afr)
{
	unsigned j;

	log_tag("smart:%s:%s\n", devinfo->file, devinfo->name);
	if (devinfo->smart[SMART_ERROR] != SMART_UNASSIGNED)
		log_tag("attr:%s:%s:error:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_ERROR]);
	if (devinfo->smart[SMART_FLAGS] != SMART_UNASSIGNED)
		log_tag("attr:%s:%s:flags:%" PRIu64 ":%" PRIx64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_FLAGS], devinfo->smart[SMART_FLAGS]);

	if (afr != 0)
		log_tag("attr:%s:%s:afr:%g:%g\n", devinfo->file, devinfo->name, afr, poisson_prob_n_or_more_failures(afr, 1));

	for (j = 0; j < 256; ++j)
		if (devinfo->smart[j] != SMART_UNASSIGNED)
			log_tag("attr:%s:%s:%u:%" PRIu64 ":%" PRIx64 "\n", devinfo->file, devinfo->name, j, devinfo->smart[j], devinfo->smart[j]);
}

static void state_smart(struct snapraid_state* state, unsigned n, tommy_list* low)
{
	tommy_node* i;
	unsigned j;
	size_t device_pad;
	size_t serial_pad;
	int have_parent;
	double array_failure_rate;
	double p_at_least_one_failure;
	int make_it_fail = 0;
	uint64_t mask32 = 0xffffffffU;
	uint64_t mask16 = 0xffffU;

	/* compute lengths for padding */
	device_pad = 0;
	serial_pad = 0;
	have_parent = 0;
	for (i = tommy_list_head(low); i != 0; i = i->next) {
		size_t len;
		devinfo_t* devinfo = i->data;

		len = strlen(devinfo->file);
		if (len > device_pad)
			device_pad = len;

		len = strlen(devinfo->serial);
		if (len > serial_pad)
			serial_pad = len;

		if (devinfo->parent != 0)
			have_parent = 1;
	}

	printf("SnapRAID SMART report:\n");
	printf("\n");
	printf("   Temp");
	printf("  Power");
	printf("   Error");
	printf("   FP");
	printf(" Size");
	printf("\n");
	printf("      C");
	printf(" OnDays");
	printf("   Count");
	printf("     ");
	printf("   TB");
	printf("  "); printl("Serial", serial_pad);
	printf("  "); printl("Device", device_pad);
	printf("  Disk");
	printf("\n");
	/*      |<##################################################################72>|####80>| */
	printf(" -----------------------------------------------------------------------\n");

	array_failure_rate = 0;
	for (i = tommy_list_head(low); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		double afr;
		uint64_t flag;

		/* clear attributes to ignore */
		for (j = 0; j < SMART_IGNORE_MAX; ++j) {
			devinfo->smart[state->smartignore[j]] = SMART_UNASSIGNED;
			devinfo->smart[devinfo->smartignore[j]] = SMART_UNASSIGNED;
		}

		if (devinfo->smart[SMART_TEMPERATURE_CELSIUS] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[SMART_TEMPERATURE_CELSIUS] & mask16);
		else if (devinfo->smart[SMART_AIRFLOW_TEMPERATURE_CELSIUS] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[SMART_AIRFLOW_TEMPERATURE_CELSIUS] & mask16);
		else
			printf("      -");

		if (devinfo->smart[SMART_POWER_ON_HOURS] != SMART_UNASSIGNED)
			printf("%7" PRIu64, (devinfo->smart[SMART_POWER_ON_HOURS] & mask32) / 24);
		else
			printf("      -");

		if (devinfo->smart[SMART_FLAGS] != SMART_UNASSIGNED)
			flag = devinfo->smart[SMART_FLAGS];
		else
			flag = 0;
		if (flag & SMARTCTL_FLAG_FAIL)
			printf("    FAIL");
		else if (flag & SMARTCTL_FLAG_PREFAIL)
			printf(" PREFAIL");
		else if (flag & SMARTCTL_FLAG_PREFAIL_LOGGED)
			printf(" logfail");
		else if (devinfo->smart[SMART_ERROR] != SMART_UNASSIGNED
			&& devinfo->smart[SMART_ERROR] != 0)
			printf("%8" PRIu64, devinfo->smart[SMART_ERROR]);
		else if (flag & SMARTCTL_FLAG_ERROR)
			printf("  logerr");
		else if (flag & SMARTCTL_FLAG_ERROR_LOGGED)
			printf(" selferr");
		else if (devinfo->smart[SMART_ERROR] == 0)
			printf("       0");
		else
			printf("       -");

		/* if some fail/prefail attribute, make the command to fail */
		if (flag & (SMARTCTL_FLAG_FAIL | SMARTCTL_FLAG_PREFAIL))
			make_it_fail = 1;

		/* note that in older smartmontools, like 5.x, rotation rate is not present */
		/* and then it could remain unassigned */

		if (flag & (SMARTCTL_FLAG_UNSUPPORTED | SMARTCTL_FLAG_OPEN)) {
			/* if error running smartctl, skip AFR estimation */
			afr = 0;
			printf("  n/a");
		} else if (devinfo->info[INFO_ROTATION_RATE] == 0) {
			/* if SSD, skip AFR estimation as data is from not SSD disks */
			afr = 0;
			printf("  SSD");
		} else {
			afr = smart_afr(devinfo->smart, devinfo->model);

			if (afr == 0) {
				/* this happens only if no data */
				printf("    -");
			} else {
				/* use only the disks in the array */
				if (devinfo->parent != 0 || !have_parent)
					array_failure_rate += afr;

				printf("%4.0f%%", poisson_prob_n_or_more_failures(afr, 1) * 100);
			}
		}

		if (devinfo->info[INFO_SIZE] != SMART_UNASSIGNED)
			printf(" %4.1f", devinfo->info[INFO_SIZE] / 1E12);
		else
			printf("    -");

		printf("  ");
		if (*devinfo->serial)
			printl(devinfo->serial, serial_pad);
		else
			printl("-", serial_pad);

		printf("  ");
		if (*devinfo->file)
			printl(devinfo->file, device_pad);
		else
			printl("-", device_pad);

		printf("  ");
		if (*devinfo->name)
			printf("%s", devinfo->name);
		else
			printf("-");

		printf("\n");

		state_info_log(devinfo);
		state_smart_log(devinfo, afr);
	}

	printf("\n");

	/*      |<##################################################################72>|####80>| */
	printf("The FP column is the estimated probability (in percentage) that the disk\n");
	printf("is going to fail in the next year.\n");
	printf("\n");

	/*
	 * The probability of one and of at least one failure is computed assuming
	 * a Poisson distribution with the estimated array failure rate.
	 */
	p_at_least_one_failure = poisson_prob_n_or_more_failures(array_failure_rate, 1);

	printf("Probability that at least one disk is going to fail in the next year is %.0f%%.\n", p_at_least_one_failure * 100);
	log_tag("summary:array_failure:%g:%g\n", array_failure_rate, p_at_least_one_failure);

	/* print extra stats only in verbose mode */
	if (msg_level < MSG_VERBOSE)
		goto bail;

	/*      |<##################################################################72>|####80>| */
	printf("\n");
	printf("Probability of data loss in the next year for different parity and\n");
	printf("combined scrub and repair time:\n");
	printf("\n");
	printf("  Parity  1 Week                1 Month             3 Months\n");
	printf(" -----------------------------------------------------------------------\n");
	for (j = 0; j < RAID_PARITY_MAX; ++j) {
		printf("%6u", j + 1);
		printf("    ");
		printp(raid_prob_of_one_or_more_failures(array_failure_rate, 365.0 / 7, n, j + 1) * 100, 19);
		printf("    ");
		printp(raid_prob_of_one_or_more_failures(array_failure_rate, 365.0 / 30, n, j + 1) * 100, 17);
		printf("    ");
		printp(raid_prob_of_one_or_more_failures(array_failure_rate, 365.0 / 90, n, j + 1) * 100, 13);
		printf("\n");
	}

	printf("\n");

	/*      |<##################################################################72>|####80>| */
	printf("These values are the probabilities that in the next year you'll have a\n");
	printf("sequence of failures that the parity WON'T be able to recover, assuming\n");
	printf("that you regularly scrub, and in case repair, the array in the specified\n");
	printf("time.\n");

bail:
	if (make_it_fail) {
		printf("\n");
		printf("DANGER! SMART is reporting that one or more disks are FAILING!\n");
		printf("Please take immediate action!\n");
		exit(EXIT_FAILURE);
	}
}

static void state_probe(struct snapraid_state* state, tommy_list* low)
{
	tommy_node* i;
	size_t device_pad;

	(void)state;

	/* compute lengths for padding */
	device_pad = 0;
	for (i = tommy_list_head(low); i != 0; i = i->next) {
		size_t len;
		devinfo_t* devinfo = i->data;

		len = strlen(devinfo->file);
		if (len > device_pad)
			device_pad = len;

		state_info_log(devinfo);
	}

	printf("SnapRAID PROBE report:\n");
	printf("\n");
	printf("   State ");
	printf("  ");
	printl("Device", device_pad);
	printf("  Disk");
	printf("\n");
	/*      |<##################################################################72>|####80>| */
	printf(" -----------------------------------------------------------------------\n");

	for (i = tommy_list_head(low); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		printf("  ");

		switch (devinfo->power) {
		case POWER_STANDBY : printf("StandBy"); break;
		case POWER_ACTIVE : printf(" Active"); break;
		default : printf("Unknown"); break;
		}

		printf("  ");
		if (*devinfo->file)
			printl(devinfo->file, device_pad);
		else
			printl("-", device_pad);

		printf("  ");
		if (*devinfo->name)
			printf("%s", devinfo->name);
		else
			printf("-");

		printf("\n");

		log_tag("probe:%s:%s:%d\n", devinfo->file, devinfo->name, devinfo->power);
	}

	printf("\n");
}

int devtest(tommy_list* high, tommy_list* low, int operation)
{
	tommy_node* i;
	unsigned count;

	if (operation != DEVICE_SMART && operation != DEVICE_PROBE)
		return -1;

	/* for each device add some fake data */
	count = 0;
	for (i = tommy_list_head(high); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		devinfo_t* entry;
		int j;

		++count;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		/* fake device number */
		entry->device = tommy_strhash_u32(0, devinfo->name);

		tommy_list_insert_tail(low, &entry->node, entry);

		for (j = 0; j < 256; ++j) {
			switch (count) {
			case 0 : entry->smart[j] = 0; break;
			case 1 : entry->smart[j] = SMART_UNASSIGNED; break;
			default :
				entry->smart[j] = 0;
				break;
			}
		}

		pathprint(entry->serial, sizeof(entry->serial), "FAKE_%s", devinfo->serial);
		pathprint(entry->vendor, sizeof(entry->vendor), "FAKE_%s", devinfo->vendor);
		pathprint(entry->model, sizeof(entry->model), "FAKE_%s", devinfo->model);
		pathprint(entry->file, sizeof(entry->file), "FAKE_%s", devinfo->file);
		pathcpy(entry->name, sizeof(entry->name), devinfo->name);
		entry->info[INFO_SIZE] = count * TERA;
		entry->info[INFO_ROTATION_RATE] = 7200;
		entry->smart[SMART_ERROR] = 0;
		entry->smart[SMART_FLAGS] = SMART_UNASSIGNED;
		entry->smart[SMART_TEMPERATURE_CELSIUS] = 27;

		switch (count) {
		case 3 : entry->smart[SMART_ERROR] = 1; break;
		case 4 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_UNSUPPORTED; break;
		case 5 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_COMMAND; break;
		case 6 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_OPEN; break;
		case 7 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_FAIL; break;
		case 8 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_PREFAIL; break;
		case 9 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_PREFAIL_LOGGED; break;
		case 10 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_ERROR; break;
		case 11 : entry->smart[SMART_FLAGS] = SMARTCTL_FLAG_ERROR_LOGGED; break;
		}

		entry->power = POWER_ACTIVE;
	}

	return 0;
}

void state_device(struct snapraid_state* state, int operation, tommy_list* filterlist_disk)
{
	tommy_node* i;
	unsigned j;
	tommy_list high;
	tommy_list low;
	int ret;

	switch (operation) {
	case DEVICE_UP : msg_progress("Spinup...\n"); break;
	case DEVICE_DOWN : msg_progress("Spindown...\n"); break;
	}

	tommy_list_init(&high);
	tommy_list_init(&low);

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		devinfo_t* entry;

		if (filterlist_disk != 0 && filter_path(filterlist_disk, 0, disk->name, 0) != 0)
			continue;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = disk->device;
		device_name_set(entry, disk->name, 0);
		pathcpy(entry->mount, sizeof(entry->mount), disk->dir);
		pathcpy(entry->smartctl, sizeof(entry->smartctl), disk->smartctl);
		memcpy(entry->smartignore, disk->smartignore, sizeof(entry->smartignore));

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		devinfo_t* entry;
		unsigned s;

		if (filterlist_disk != 0 && filter_path(filterlist_disk, 0, lev_config_name(j), 0) != 0)
			continue;

		for (s = 0; s < state->parity[j].split_mac; ++s) {
			entry = calloc_nofail(1, sizeof(devinfo_t));

			entry->device = state->parity[j].split_map[s].device;
			device_name_set(entry, lev_config_name(j), s);
			pathcpy(entry->mount, sizeof(entry->mount), state->parity[j].split_map[s].path);
			pathcpy(entry->smartctl, sizeof(entry->smartctl), state->parity[j].smartctl);
			memcpy(entry->smartignore, state->parity[j].smartignore, sizeof(entry->smartignore));
			pathcut(entry->mount); /* remove the parity file */

			tommy_list_insert_tail(&high, &entry->node, entry);
		}
	}

	if (state->opt.fake_device) {
		ret = devtest(&high, &low, operation);
	} else {
		int others = operation == DEVICE_SMART || operation == DEVICE_PROBE;

		ret = devquery(&high, &low, operation, others);
	}

	/* if the list is empty, it's not supported in this platform */
	if (ret == 0 && tommy_list_empty(&low))
		ret = -1;

	if (ret != 0) {
		const char* ope = 0;
		switch (operation) {
		case DEVICE_UP : ope = "Spinup"; break;
		case DEVICE_DOWN : ope = "Spindown"; break;
		case DEVICE_LIST : ope = "Device listing"; break;
		case DEVICE_SMART : ope = "Smart"; break;
		case DEVICE_PROBE : ope = "Probe"; break;
		case DEVICE_DOWNIFUP : ope = "Spindown"; break;
		}
		log_fatal("%s is unsupported in this platform.\n", ope);
	} else {
		if (operation == DEVICE_LIST) {
			for (i = tommy_list_head(&low); i != 0; i = i->next) {
				devinfo_t* devinfo = i->data;
				devinfo_t* parent = devinfo->parent;
#ifdef _WIN32
				printf("%" PRIu64 "\t%s\t%08" PRIx64 "\t%s\t%s\n", devinfo->device, devinfo->wfile, parent->device, parent->wfile, parent->name);
#else
				printf("%u:%u\t%s\t%u:%u\t%s\t%s\n", major(devinfo->device), minor(devinfo->device), devinfo->file, major(parent->device), minor(parent->device), parent->file, parent->name);
#endif
			}
		}

		if (operation == DEVICE_SMART)
			state_smart(state, state->level + tommy_list_count(&state->disklist), &low);

		if (operation == DEVICE_PROBE)
			state_probe(state, &low);
	}

	tommy_list_foreach(&high, free);
	tommy_list_foreach(&low, free);
}

