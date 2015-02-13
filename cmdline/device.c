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
 * becasue it's the period when more SMART attributes were gathered.
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
unsigned SMART_5_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 5 at a given value.
 */
double SMART_5_R[SMART_MEASURES] = {
	0.0034, 0.0738, 0.0883, 0.0960, 0.1016,
	0.1062, 0.1106, 0.1142, 0.1189, 0.1259,
	0.1307, 0.1337, 0.1368, 0.1400, 0.1419,
	0.1448, 0.1463, 0.1497, 0.1512, 0.1532,
	0.1546, 0.1563, 0.1582, 0.1606, 0.1624,
	0.1645, 0.1658, 0.1670, 0.1687, 0.1702,
	0.1717, 0.1730, 0.1749, 0.1774, 0.1791,
	0.1804, 0.1819, 0.1826, 0.1840, 0.1858,
	0.1869, 0.1899, 0.1906, 0.1914, 0.1932,
	0.1942, 0.1954, 0.1962, 0.1985, 0.2007,
	0.2017, 0.2030, 0.2041, 0.2049, 0.2061,
	0.2072, 0.2080, 0.2096, 0.2100, 0.2107,
	0.2113, 0.2124, 0.2132, 0.2138, 0.2145,
	0.2159, 0.2168, 0.2180, 0.2198, 0.2204,
	0.2208, 0.2212, 0.2225, 0.2242, 0.2248,
	0.2254, 0.2261, 0.2265, 0.2269, 0.2278,
	0.2293, 0.2313, 0.2321, 0.2326, 0.2334,
	0.2346, 0.2362, 0.2366, 0.2375, 0.2393,
	0.2397, 0.2402, 0.2410, 0.2415, 0.2423,
	0.2436, 0.2447, 0.2458, 0.2463, 0.2470,
	0.2481, 0.2488, 0.2492, 0.2498, 0.2510,
	0.2528, 0.2536, 0.2540, 0.2548, 0.2557,
	0.2563, 0.2568, 0.2574, 0.2591, 0.2593,
	0.2596, 0.2603, 0.2608, 0.2613, 0.2616,
	0.2622, 0.2635, 0.2648, 0.2651, 0.2658,
	0.2661, 0.2664, 0.2667, 0.2677, 0.2689,
	0.2697, 0.2699, 0.2701, 0.2711, 0.2717,
	0.2719, 0.2733, 0.2745, 0.2759, 0.2761,
	0.2769, 0.2771, 0.2772, 0.2774, 0.2780,
	0.2796, 0.2798, 0.2803, 0.2808, 0.2811,
	0.2814, 0.2821, 0.2829, 0.2841, 0.2846,
	0.2848, 0.2851, 0.2856, 0.2866, 0.2869,
	0.2873, 0.2895, 0.2897, 0.2903, 0.2906,
	0.2910, 0.2914, 0.2916, 0.2923, 0.2940,
	0.2945, 0.2954, 0.2959, 0.2962, 0.2966,
	0.2974, 0.2991, 0.3004, 0.3004, 0.3007,
	0.3011, 0.3015, 0.3017, 0.3019, 0.3025,
	0.3037, 0.3038, 0.3040, 0.3043, 0.3046,
	0.3050, 0.3055, 0.3061, 0.3079, 0.3084,
	0.3088, 0.3092, 0.3095, 0.3101, 0.3109,
	0.3119, 0.3142, 0.3146, 0.3149, 0.3151,
	0.3153, 0.3154, 0.3157, 0.3164, 0.3177,
	0.3178, 0.3186, 0.3188, 0.3201, 0.3208,
	0.3212, 0.3218, 0.3233, 0.3234, 0.3239,
	0.3243, 0.3246, 0.3256, 0.3258, 0.3264,
	0.3280, 0.3285, 0.3288, 0.3297, 0.3299,
	0.3302, 0.3304, 0.3310, 0.3323, 0.3324,
	0.3326, 0.3327, 0.3328, 0.3337, 0.3338,
	0.3349, 0.3367, 0.3374, 0.3378, 0.3381,
	0.3385, 0.3387, 0.3389, 0.3394, 0.3404,
	0.3408, 0.3410, 0.3412, 0.3413, 0.3416,
	0.3421, 
};

/*
 * Divider for SMART attribute 187
 */
unsigned SMART_187_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 187 at a given value.
 */
double SMART_187_R[SMART_MEASURES] = {
	0.0048, 0.0957, 0.1157, 0.1306, 0.1415,
	0.1501, 0.1695, 0.2444, 0.2714, 0.2925,
	0.3113, 0.3284, 0.3515, 0.4054, 0.4331,
	0.4578, 0.4817, 0.5010, 0.5171, 0.5610,
	0.5893, 0.6120, 0.6362, 0.6608, 0.6803,
	0.7261, 0.7490, 0.7818, 0.8109, 0.8364,
	0.8599, 0.9086, 0.9303, 0.9580, 0.9820,
	1.0057, 1.0388, 1.0803, 1.1067, 1.1327,
	1.1589, 1.1788, 1.1963, 1.2136, 1.2367,
	1.2617, 1.2789, 1.2940, 1.3134, 1.3376,
	1.3562, 1.3673, 1.3799, 1.4099, 1.4260,
	1.4321, 1.4495, 1.4548, 1.4696, 1.4816,
	1.5054, 1.5204, 1.5296, 1.5627, 1.5820,
	1.5969, 1.6076, 1.6125, 1.6205, 1.6333,
	1.6582, 1.6674, 1.6843, 1.7144, 1.7269,
	1.7494, 1.7590, 1.7747, 1.7962, 1.8383,
	1.8480, 1.8532, 1.8641, 1.8793, 1.8850,
	1.8920, 1.8978, 1.9033, 1.9081, 1.9237,
	1.9609, 1.9805, 2.0055, 2.0095, 2.0127,
	2.0277, 2.0493, 2.0873, 2.1064, 2.1130,
	2.1190, 2.1240, 2.1477, 2.1850, 2.1979,
	2.2168, 2.2279, 2.2701, 2.2794, 2.2855,
	2.3012, 2.3138, 2.3167, 2.3764, 2.3832,
	2.3860, 2.3906, 2.4012, 2.4261, 2.4314,
	2.4539, 2.4556, 2.4630, 2.4764, 2.4821,
	2.4851, 2.4894, 2.4922, 2.4992, 2.5123,
	2.5202, 2.5232, 2.5264, 2.5322, 2.5407,
	2.5429, 2.5485, 2.5642, 2.5669, 2.5743,
	2.5887, 2.5890, 2.6177, 2.7026, 2.7028,
	2.7080, 2.7110, 2.7159, 2.7192, 2.7217,
	2.7227, 2.7263, 2.7313, 2.7344, 2.7348,
	2.7450, 2.7501, 2.7641, 2.7670, 2.7711,
	2.7724, 2.7753, 2.7757, 2.7853, 2.7870,
	2.7919, 2.7947, 2.7974, 2.8002, 2.8132,
	2.8157, 2.8195, 2.8204, 2.8220, 2.8254,
	2.8270, 2.8329, 2.8479, 2.8490, 2.8532,
	2.8578, 2.8662, 2.8710, 2.8737, 2.8742,
	2.8750, 2.9768, 2.9810, 2.9825, 2.9873,
	2.9884, 3.0040, 3.0055, 3.0066, 3.0119,
	3.0189, 3.0239, 3.0277, 3.0335, 3.0345,
	3.0388, 3.0521, 3.0673, 3.0717, 3.0734,
	3.0753, 3.0770, 3.0961, 3.0975, 3.0988,
	3.1004, 3.1018, 3.1018, 3.1041, 3.1058,
	3.1067, 3.1126, 3.1144, 3.1178, 3.1185,
	3.1226, 3.1264, 3.1303, 3.1308, 3.1314,
	3.1339, 3.1346, 3.1364, 3.1384, 3.1389,
	3.1390, 3.1399, 3.1405, 3.1406, 3.1412,
	3.1412, 3.1424, 3.1467, 3.1556, 3.1650,
	3.1662, 3.1834, 3.1839, 3.1853, 3.1855,
	3.1864, 3.1889, 3.1912, 3.1913, 3.1918,
	3.1924, 3.1934, 3.1951, 3.2129, 3.2138,
	3.2176, 
};

/*
 * Divider for SMART attribute 188
 */
unsigned SMART_188_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 188 at a given value.
 */
double SMART_188_R[SMART_MEASURES] = {
	0.0028, 0.0312, 0.0493, 0.0625, 0.0712,
	0.0773, 0.0818, 0.0891, 0.0950, 0.1006,
	0.1054, 0.1093, 0.1129, 0.1193, 0.1241,
	0.1289, 0.1329, 0.1367, 0.1406, 0.1452,
	0.1498, 0.1541, 0.1555, 0.1584, 0.1594,
	0.1620, 0.1648, 0.1678, 0.1698, 0.1716,
	0.1723, 0.1754, 0.1775, 0.1830, 0.1872,
	0.1940, 0.1969, 0.2038, 0.2051, 0.2170,
	0.2177, 0.2273, 0.2289, 0.2395, 0.2397,
	0.2487, 0.2493, 0.2577, 0.2610, 0.2713,
	0.2719, 0.2796, 0.2824, 0.2935, 0.2940,
	0.3023, 0.3043, 0.3148, 0.3161, 0.3225,
	0.3228, 0.3287, 0.3287, 0.3347, 0.3347,
	0.3403, 0.3403, 0.3455, 0.3505, 0.3551,
	0.3551, 0.3583, 0.3583, 0.3625, 0.3625,
	0.3648, 0.3648, 0.3667, 0.3667, 0.3698,
	0.3729, 0.3732, 0.3732, 0.3782, 0.3782,
	0.3835, 0.3863, 0.3864, 0.3864, 0.3899,
	0.3899, 0.3901, 0.3935, 0.3982, 0.4021,
	0.4039, 0.4039, 0.4060, 0.4060, 0.4084,
	0.4087, 0.4108, 0.4110, 0.4114, 0.4115,
	0.4172, 0.4219, 0.4297, 0.4320, 0.4334,
	0.4335, 0.4356, 0.4358, 0.4376, 0.4388,
	0.4396, 0.4397, 0.4401, 0.4402, 0.4403,
	0.4403, 0.4407, 0.4408, 0.4410, 0.4467,
	0.4478, 0.4494, 0.4501, 0.4501, 0.4511,
	0.4511, 0.4513, 0.4514, 0.4515, 0.4515,
	0.4515, 0.4517, 0.4518, 0.4518, 0.4519,
	0.4519, 0.4520, 0.4520, 0.4521, 0.4533,
	0.4533, 0.4533, 0.4536, 0.4536, 0.4536,
	0.4536, 0.4569, 0.4581, 0.4581, 0.4609,
	0.4610, 0.4614, 0.4662, 0.4680, 0.4693,
	0.4693, 0.4693, 0.4706, 0.4709, 0.4743,
	0.4743, 0.4743, 0.4743, 0.4743, 0.4744,
	0.4744, 0.4746, 0.4746, 0.4746, 0.4746,
	0.4746, 0.4746, 0.4746, 0.4746, 0.4746,
	0.4746, 0.4746, 0.4746, 0.4746, 0.4746,
	0.4746, 0.4746, 0.4746, 0.4746, 0.4746,
	0.4746, 0.4746, 0.4746, 0.4778, 0.4778,
	0.4778, 0.4778, 0.4810, 0.4810, 0.4810,
	0.4810, 0.4810, 0.4810, 0.4810, 0.4811,
	0.4811, 0.4811, 0.4811, 0.4811, 0.4811,
	0.4811, 0.4811, 0.4811, 0.4811, 0.4811,
	0.4812, 0.4812, 0.4812, 0.4812, 0.4812,
	0.4812, 0.4812, 0.4812, 0.4812, 0.4812,
	0.4862, 0.4916, 0.4916, 0.4916, 0.4917,
	0.4917, 0.4917, 0.4917, 0.4972, 0.4972,
	0.4973, 0.4973, 0.4973, 0.4973, 0.4973,
	0.5030, 0.5031, 0.5031, 0.5033, 0.5033,
	0.5033, 0.5033, 0.5034, 0.5034, 0.5034,
	0.5034, 0.5034, 0.5034, 0.5034, 0.5034,
	0.5034, 
};

/*
 * Divider for SMART attribute 193
 */
unsigned SMART_193_STEP = 1626;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 193 at a given value.
 */
double SMART_193_R[SMART_MEASURES] = {
	0.0000, 0.0029, 0.0033, 0.0040, 0.0046,
	0.0051, 0.0059, 0.0065, 0.0073, 0.0080,
	0.0088, 0.0096, 0.0104, 0.0112, 0.0117,
	0.0125, 0.0132, 0.0139, 0.0149, 0.0158,
	0.0167, 0.0172, 0.0179, 0.0189, 0.0202,
	0.0216, 0.0226, 0.0242, 0.0253, 0.0264,
	0.0279, 0.0293, 0.0306, 0.0319, 0.0330,
	0.0345, 0.0356, 0.0370, 0.0388, 0.0410,
	0.0426, 0.0445, 0.0463, 0.0475, 0.0487,
	0.0499, 0.0513, 0.0530, 0.0548, 0.0565,
	0.0585, 0.0606, 0.0627, 0.0644, 0.0663,
	0.0683, 0.0711, 0.0735, 0.0765, 0.0790,
	0.0812, 0.0837, 0.0862, 0.0882, 0.0911,
	0.0941, 0.0970, 0.0993, 0.1026, 0.1063,
	0.1094, 0.1129, 0.1162, 0.1191, 0.1221,
	0.1256, 0.1281, 0.1315, 0.1347, 0.1386,
	0.1420, 0.1446, 0.1475, 0.1501, 0.1533,
	0.1568, 0.1600, 0.1634, 0.1678, 0.1718,
	0.1750, 0.1788, 0.1818, 0.1848, 0.1875,
	0.1901, 0.1930, 0.1961, 0.1996, 0.2025,
	0.2055, 0.2087, 0.2115, 0.2145, 0.2171,
	0.2203, 0.2234, 0.2265, 0.2300, 0.2331,
	0.2364, 0.2392, 0.2419, 0.2450, 0.2494,
	0.2535, 0.2571, 0.2605, 0.2643, 0.2690,
	0.2738, 0.2781, 0.2828, 0.2863, 0.2910,
	0.2947, 0.2995, 0.3026, 0.3065, 0.3118,
	0.3169, 0.3234, 0.3284, 0.3331, 0.3376,
	0.3405, 0.3447, 0.3484, 0.3517, 0.3557,
	0.3595, 0.3635, 0.3681, 0.3750, 0.3824,
	0.3897, 0.3988, 0.4070, 0.4143, 0.4206,
	0.4255, 0.4310, 0.4360, 0.4411, 0.4456,
	0.4500, 0.4544, 0.4597, 0.4643, 0.4695,
	0.4740, 0.4791, 0.4839, 0.4891, 0.4940,
	0.4981, 0.5028, 0.5077, 0.5124, 0.5175,
	0.5224, 0.5275, 0.5334, 0.5385, 0.5435,
	0.5493, 0.5544, 0.5598, 0.5670, 0.5739,
	0.5794, 0.5840, 0.5890, 0.5945, 0.6000,
	0.6060, 0.6113, 0.6201, 0.6258, 0.6350,
	0.6404, 0.6459, 0.6518, 0.6579, 0.6659,
	0.6736, 0.6817, 0.6891, 0.6988, 0.7076,
	0.7168, 0.7257, 0.7354, 0.7435, 0.7547,
	0.7651, 0.7780, 0.7892, 0.7977, 0.8093,
	0.8200, 0.8280, 0.8356, 0.8495, 0.8576,
	0.8695, 0.8799, 0.8914, 0.8997, 0.9079,
	0.9162, 0.9283, 0.9368, 0.9475, 0.9568,
	0.9660, 0.9737, 0.9815, 0.9898, 0.9988,
	1.0075, 1.0165, 1.0256, 1.0338, 1.0422,
	1.0533, 1.0653, 1.0760, 1.0853, 1.0975,
	1.1075, 1.1172, 1.1291, 1.1414, 1.1561,
	1.1666, 1.1798, 1.1940, 1.2073, 1.2189,
	1.2324, 1.2436, 1.2557, 1.2699, 1.2811,
	1.2945, 
};

/*
 * Divider for SMART attribute 197
 */
unsigned SMART_197_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 197 at a given value.
 */
double SMART_197_R[SMART_MEASURES] = {
	0.0036, 0.2430, 0.3027, 0.3349, 0.3533,
	0.3632, 0.3722, 0.3790, 0.4199, 0.6417,
	0.6517, 0.6583, 0.6647, 0.6680, 0.6737,
	0.6787, 0.7076, 0.9073, 0.9113, 0.9194,
	0.9238, 0.9326, 0.9394, 0.9446, 0.9697,
	1.1417, 1.1466, 1.1485, 1.1522, 1.1775,
	1.1817, 1.1864, 1.2105, 1.3939, 1.3991,
	1.4013, 1.4125, 1.4135, 1.4210, 1.4217,
	1.4335, 1.6024, 1.6040, 1.6132, 1.6174,
	1.6187, 1.6203, 1.6211, 1.6354, 1.7839,
	1.7858, 1.7869, 1.7889, 1.7911, 1.7932,
	1.7962, 1.8091, 1.9381, 1.9405, 1.9410,
	1.9415, 1.9420, 1.9424, 1.9434, 1.9518,
	2.0516, 2.0518, 2.0521, 2.0524, 2.0532,
	2.0538, 2.0550, 2.0624, 2.1508, 2.1514,
	2.1519, 2.1519, 2.1520, 2.1533, 2.1534,
	2.1603, 2.2730, 2.2730, 2.2730, 2.2757,
	2.2768, 2.2774, 2.2778, 2.2844, 2.3740,
	2.3753, 2.3769, 2.3779, 2.3790, 2.3796,
	2.3806, 2.3891, 2.5538, 2.5550, 2.5563,
	2.5588, 2.5594, 2.5600, 2.5605, 2.5665,
	2.7510, 2.7526, 2.7539, 2.7544, 2.7549,
	2.7552, 2.7555, 2.7590, 2.8782, 2.8788,
	2.8812, 2.8821, 2.8824, 2.8836, 2.8836,
	2.8864, 2.9536, 2.9539, 2.9543, 2.9559,
	2.9565, 2.9572, 2.9572, 2.9601, 3.0230,
	3.0230, 3.0230, 3.0230, 3.0230, 3.0231,
	3.0231, 3.0266, 3.0846, 3.0846, 3.0846,
	3.0846, 3.0846, 3.0847, 3.0851, 3.0874,
	3.1242, 3.1242, 3.1242, 3.1242, 3.1246,
	3.1246, 3.1246, 3.1273, 3.1806, 3.1806,
	3.1806, 3.1806, 3.1806, 3.1810, 3.1814,
	3.1829, 3.2173, 3.2174, 3.2174, 3.2174,
	3.2174, 3.2174, 3.2174, 3.2197, 3.2643,
	3.2643, 3.2643, 3.2661, 3.2661, 3.2666,
	3.2666, 3.2677, 3.2962, 3.2962, 3.2963,
	3.3673, 3.3713, 3.3813, 3.3813, 3.3835,
	3.4059, 3.4059, 3.4065, 3.4091, 3.4096,
	3.4103, 3.4104, 3.4123, 3.4316, 3.4360,
	3.4387, 3.4393, 3.4393, 3.4404, 3.4409,
	3.4440, 3.5547, 3.5547, 3.5547, 3.5547,
	3.5547, 3.5547, 3.5548, 3.5558, 3.5801,
	3.5801, 3.5801, 3.5801, 3.5802, 3.5802,
	3.5802, 3.5820, 3.6178, 3.6178, 3.6178,
	3.6178, 3.6178, 3.6242, 3.6242, 3.6260,
	3.6567, 3.6567, 3.6568, 3.6568, 3.6568,
	3.6568, 3.6831, 3.6841, 3.7260, 3.7268,
	3.7269, 3.7269, 3.7269, 3.7269, 3.7269,
	3.7279, 3.7387, 3.7387, 3.7394, 3.7394,
	3.7394, 3.7394, 3.7394, 3.7402, 3.7622,
	3.7622, 3.7629, 3.7734, 3.7734, 3.7734,
	3.7734, 
};

/*
 * Divider for SMART attribute 198
 */
unsigned SMART_198_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 198 at a given value.
 */
double SMART_198_R[SMART_MEASURES] = {
	0.0038, 0.3963, 0.4189, 0.4297, 0.4364,
	0.4393, 0.4433, 0.4490, 0.4909, 0.7634,
	0.7649, 0.7683, 0.7689, 0.7703, 0.7717,
	0.7737, 0.8009, 1.0435, 1.0438, 1.0445,
	1.0446, 1.0456, 1.0496, 1.0501, 1.0695,
	1.2652, 1.2662, 1.2667, 1.2707, 1.2722,
	1.2734, 1.2750, 1.2974, 1.5036, 1.5038,
	1.5050, 1.5156, 1.5156, 1.5231, 1.5239,
	1.5347, 1.7251, 1.7258, 1.7271, 1.7271,
	1.7271, 1.7271, 1.7271, 1.7423, 1.9132,
	1.9134, 1.9134, 1.9134, 1.9134, 1.9135,
	1.9135, 1.9221, 2.0645, 2.0645, 2.0645,
	2.0645, 2.0645, 2.0645, 2.0645, 2.0716,
	2.1838, 2.1838, 2.1838, 2.1838, 2.1838,
	2.1838, 2.1838, 2.1910, 2.2936, 2.2937,
	2.2937, 2.2937, 2.2937, 2.2937, 2.2937,
	2.3007, 2.4350, 2.4350, 2.4350, 2.4352,
	2.4352, 2.4352, 2.4352, 2.4407, 2.5482,
	2.5482, 2.5482, 2.5482, 2.5482, 2.5482,
	2.5482, 2.5539, 2.7538, 2.7538, 2.7538,
	2.7538, 2.7538, 2.7538, 2.7539, 2.7585,
	2.9231, 2.9231, 2.9231, 2.9231, 2.9231,
	2.9232, 2.9232, 2.9264, 3.0707, 3.0707,
	3.0707, 3.0707, 3.0707, 3.0707, 3.0707,
	3.0734, 3.1562, 3.1562, 3.1562, 3.1562,
	3.1562, 3.1562, 3.1562, 3.1584, 3.2363,
	3.2363, 3.2363, 3.2363, 3.2363, 3.2363,
	3.2363, 3.2397, 3.3121, 3.3121, 3.3121,
	3.3121, 3.3121, 3.3122, 3.3122, 3.3142,
	3.3612, 3.3612, 3.3612, 3.3612, 3.3612,
	3.3612, 3.3612, 3.3636, 3.4314, 3.4314,
	3.4314, 3.4314, 3.4314, 3.4314, 3.4314,
	3.4325, 3.4769, 3.4769, 3.4769, 3.4769,
	3.4769, 3.4769, 3.4769, 3.4788, 3.5366,
	3.5366, 3.5366, 3.5366, 3.5366, 3.5366,
	3.5366, 3.5377, 3.5756, 3.5756, 3.5756,
	3.5756, 3.5756, 3.5756, 3.5756, 3.5775,
	3.6049, 3.6049, 3.6049, 3.6049, 3.6049,
	3.6049, 3.6049, 3.6061, 3.6279, 3.6279,
	3.6279, 3.6279, 3.6279, 3.6279, 3.6279,
	3.6309, 3.7670, 3.7670, 3.7670, 3.7670,
	3.7670, 3.7670, 3.7670, 3.7679, 3.7983,
	3.7983, 3.7983, 3.7983, 3.7983, 3.7983,
	3.7983, 3.8000, 3.8449, 3.8449, 3.8449,
	3.8449, 3.8449, 3.8449, 3.8449, 3.8467,
	3.8854, 3.9337, 3.9337, 3.9337, 3.9337,
	3.9337, 3.9337, 3.9346, 3.9892, 3.9892,
	3.9892, 3.9892, 3.9892, 3.9892, 3.9892,
	3.9901, 4.0043, 4.0043, 4.0043, 4.0043,
	4.0043, 4.0043, 4.0043, 4.0050, 4.0340,
	4.0340, 4.0340, 4.0340, 4.0340, 4.0340,
	4.0340, 
};

/*
 * Divider for SMART attribute 199
 */
unsigned SMART_199_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 199 at a given value.
 */
double SMART_199_R[SMART_MEASURES] = {
	0.0037, 0.0449, 0.0680, 0.0872, 0.1023,
	0.1153, 0.1272, 0.1375, 0.1462, 0.1564,
	0.1641, 0.1722, 0.1790, 0.1846, 0.1915,
	0.1973, 0.2020, 0.2086, 0.2120, 0.2177,
	0.2240, 0.2318, 0.2377, 0.2450, 0.2482,
	0.2532, 0.2571, 0.2611, 0.2652, 0.2688,
	0.2719, 0.2740, 0.2762, 0.2794, 0.2834,
	0.2871, 0.2902, 0.2932, 0.2969, 0.2990,
	0.3009, 0.3021, 0.3057, 0.3085, 0.3101,
	0.3139, 0.3155, 0.3198, 0.3221, 0.3243,
	0.3249, 0.3277, 0.3309, 0.3327, 0.3346,
	0.3380, 0.3408, 0.3444, 0.3457, 0.3475,
	0.3496, 0.3509, 0.3536, 0.3555, 0.3565,
	0.3589, 0.3606, 0.3642, 0.3655, 0.3669,
	0.3696, 0.3740, 0.3761, 0.3782, 0.3794,
	0.3815, 0.3818, 0.3847, 0.3855, 0.3864,
	0.3885, 0.3898, 0.3914, 0.3936, 0.3956,
	0.3972, 0.4003, 0.4008, 0.4010, 0.4026,
	0.4037, 0.4041, 0.4044, 0.4058, 0.4086,
	0.4091, 0.4108, 0.4111, 0.4113, 0.4137,
	0.4148, 0.4149, 0.4157, 0.4174, 0.4184,
	0.4195, 0.4205, 0.4215, 0.4217, 0.4245,
	0.4247, 0.4248, 0.4266, 0.4287, 0.4292,
	0.4300, 0.4312, 0.4314, 0.4320, 0.4351,
	0.4373, 0.4388, 0.4398, 0.4404, 0.4411,
	0.4424, 0.4435, 0.4446, 0.4476, 0.4479,
	0.4495, 0.4495, 0.4500, 0.4503, 0.4507,
	0.4509, 0.4509, 0.4511, 0.4522, 0.4527,
	0.4533, 0.4533, 0.4552, 0.4554, 0.4562,
	0.4562, 0.4586, 0.4607, 0.4607, 0.4611,
	0.4625, 0.4645, 0.4668, 0.4686, 0.4699,
	0.4700, 0.4712, 0.4724, 0.4737, 0.4764,
	0.4767, 0.4770, 0.4772, 0.4778, 0.4794,
	0.4822, 0.4830, 0.4847, 0.4859, 0.4863,
	0.4871, 0.4888, 0.4888, 0.4901, 0.4901,
	0.4915, 0.4917, 0.4924, 0.4943, 0.4957,
	0.4957, 0.4973, 0.4983, 0.4995, 0.5008,
	0.5008, 0.5028, 0.5031, 0.5044, 0.5072,
	0.5072, 0.5086, 0.5086, 0.5087, 0.5087,
	0.5089, 0.5093, 0.5096, 0.5115, 0.5122,
	0.5132, 0.5142, 0.5157, 0.5158, 0.5172,
	0.5174, 0.5204, 0.5219, 0.5219, 0.5243,
	0.5257, 0.5268, 0.5268, 0.5278, 0.5295,
	0.5301, 0.5301, 0.5322, 0.5322, 0.5328,
	0.5345, 0.5360, 0.5360, 0.5382, 0.5397,
	0.5397, 0.5397, 0.5397, 0.5399, 0.5399,
	0.5416, 0.5420, 0.5431, 0.5438, 0.5446,
	0.5449, 0.5466, 0.5467, 0.5467, 0.5467,
	0.5467, 0.5491, 0.5498, 0.5499, 0.5548,
	0.5565, 0.5566, 0.5566, 0.5575, 0.5614,
	0.5632, 0.5632, 0.5632, 0.5632, 0.5633,
	0.5662, 
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
 * Computes the estimated Annual Failure Rare of a set of SMART attributes.
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
static double smart_afr(uint64_t* smart)
{
	double afr = 0;

	if (smart[5] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_5_R, SMART_5_STEP, smart[5]);
		if (afr < r)
			afr = r;
	}

	if (smart[187] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_187_R, SMART_187_STEP, smart[187]);
		if (afr < r)
			afr = r;
	}

	if (smart[188] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_188_R, SMART_188_STEP, smart[188]);
		if (afr < r)
			afr = r;
	}

	if (smart[193] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_193_R, SMART_193_STEP, smart[193]);
		if (afr < r)
			afr = r;
	}

	if (smart[197] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_197_R, SMART_197_STEP, smart[197]);
		if (afr < r)
			afr = r;
	}

	if (smart[198] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_198_R, SMART_198_STEP, smart[198]);
		if (afr < r)
			afr = r;
	}

	if (smart[199] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_199_R, SMART_199_STEP, smart[199]);
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
	/* note that is almost equal at the probabilty of */
	/* the first failure. */
	return poisson_prob_n_or_more_failures(raid_failure_rate, 1);
}

/**
 * Prints a string with space padding.
 */
static void printl(const char* str, size_t pad)
{
	size_t len;

	printf("%s", str);

	len = strlen(str);

	while (len < pad) {
		printf(" ");
		++len;
	}
}

/**
 * Prints a probability with space padding.
 */
static void printp(double v, size_t pad)
{
	char buf[64];
	const char* s = "%";

	if (v > 0.1)
		snprintf(buf, sizeof(buf), "%5.2f%s", v, s);
	else if (v > 0.01)
		snprintf(buf, sizeof(buf), "%6.3f%s", v, s);
	else if (v > 0.001)
		snprintf(buf, sizeof(buf), "%7.4f%s", v, s);
	else if (v > 0.0001)
		snprintf(buf, sizeof(buf), "%8.5f%s", v, s);
	else if (v > 0.00001)
		snprintf(buf, sizeof(buf), "%9.6f%s", v, s);
	else if (v > 0.000001)
		snprintf(buf, sizeof(buf), "%10.7f%s", v, s);
	else if (v > 0.0000001)
		snprintf(buf, sizeof(buf), "%11.8f%s", v, s);
	else if (v > 0.00000001)
		snprintf(buf, sizeof(buf), "%12.9f%s", v, s);
	else if (v > 0.000000001)
		snprintf(buf, sizeof(buf), "%13.10f%s", v, s);
	else if (v > 0.0000000001)
		snprintf(buf, sizeof(buf), "%14.11f%s", v, s);
	else if (v > 0.00000000001)
		snprintf(buf, sizeof(buf), "%15.12f%s", v, s);
	else if (v > 0.000000000001)
		snprintf(buf, sizeof(buf), "%16.13f%s", v, s);
	else
		snprintf(buf, sizeof(buf), "%17.14f%s", v, s);
	printl(buf, pad);
}

static void state_smart(int verbose, unsigned n, tommy_list* low)
{
	tommy_node* i;
	unsigned j;
	size_t device_pad;
	size_t serial_pad;
	int have_parent;
	double array_failure_rate;
	double p_at_least_one_failure;
	int make_it_fail = 0;

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

		len = strlen(devinfo->smart_serial);
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

		if (devinfo->smart[194] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[194]);
		else if (devinfo->smart[190] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[190]);
		else
			printf("      -");

		if (devinfo->smart[9] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[9] / 24);
		else
			printf("      -");

		if (devinfo->smart[SMART_FLAGS] != SMART_UNASSIGNED)
			flag = devinfo->smart[SMART_FLAGS];
		else
			flag = 0;
		if (flag & SMARTCTL_FLAG_FAIL)
			printf("    FAIL");
		else if (flag & (SMARTCTL_FLAG_PREFAIL | SMARTCTL_FLAG_PREFAIL_LOGGED))
			printf("  PREFAIL");
		else if (devinfo->smart[SMART_ERROR] != SMART_UNASSIGNED
			&& devinfo->smart[SMART_ERROR] != 0)
			printf("%8" PRIu64, devinfo->smart[SMART_ERROR]);
		else if (flag & (SMARTCTL_FLAG_ERROR | SMARTCTL_FLAG_ERROR_LOGGED))
			printf("   ERROR");
		else if (flag & (SMARTCTL_FLAG_UNSUPPORTED | SMARTCTL_FLAG_OPEN))
			printf("       -");
		else if (devinfo->smart[SMART_ERROR] == 0)
			printf("       0");
		else
			printf("       -");

		/* if some fail/prefail attribute, make the command to fail */
		if (flag & (SMARTCTL_FLAG_FAIL | SMARTCTL_FLAG_PREFAIL | SMARTCTL_FLAG_PREFAIL_LOGGED))
			make_it_fail = 1;

		if (flag & (SMARTCTL_FLAG_UNSUPPORTED | SMARTCTL_FLAG_OPEN)) {
			/* if error running smartctl, skip AFR estimation */
			afr = 0;
			printf("    -");
		} else if (devinfo->smart[SMART_ROTATION_RATE] == 0) {
			/* if SSD, skip AFR estimation as data is from not SSD disks */
			afr = 0;
			printf("  SSD");
		} else {
			afr = smart_afr(devinfo->smart);

			/* use only the disks in the array */
			if (devinfo->parent != 0 || !have_parent)
				array_failure_rate += afr;

			printf("%4.0f%%", poisson_prob_n_or_more_failures(afr, 1) * 100);
		}

		if (devinfo->smart[SMART_SIZE] != SMART_UNASSIGNED)
			printf("  %2.1f", devinfo->smart[SMART_SIZE] / 1E12);
		else
			printf("    -");

		printf("  ");
		if (*devinfo->smart_serial)
			printl(devinfo->smart_serial, serial_pad);
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

		ftag("smart:%s:%s:%s:%g\n", devinfo->file, devinfo->name, esc(devinfo->smart_serial), afr);
		for (j = 0; j < 256; ++j) {
			if (devinfo->smart[j] != SMART_UNASSIGNED)
				ftag("attr:%s:%u:%" PRIu64 "\n", devinfo->file, j, devinfo->smart[j]);
		}
		if (devinfo->smart[SMART_SIZE] != SMART_UNASSIGNED)
			ftag("attr:%s:size:%" PRIu64 "\n", devinfo->file, devinfo->smart[SMART_SIZE]);
		if (devinfo->smart[SMART_ERROR] != SMART_UNASSIGNED)
			ftag("attr:%s:error:%" PRIu64 "\n", devinfo->file, devinfo->smart[SMART_ERROR]);
		if (devinfo->smart[SMART_ROTATION_RATE] != SMART_UNASSIGNED)
			ftag("attr:%s:rotationrate:%" PRIu64 "\n", devinfo->file, devinfo->smart[SMART_ROTATION_RATE]);
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

	printf("Probability that one disk is going to fail in the next year is %.0f%%,\n", p_at_least_one_failure * 100);
	ftag("summary:array_failure:%g:%g\n", array_failure_rate, p_at_least_one_failure);

	/* prints extra stats only in verbose mode */
	if (!verbose)
		return;

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
	printf("sequence of failures that the parity WONT be able to recover, assuming\n");
	printf("that you regularly scrub, and in case repair, the array in the specified\n");
	printf("time.\n");

	if (make_it_fail) {
		/* LCOV_EXCL_START */
		printf("\n");
		printf("DANGER! SMART is reporting that one or more disks are FAILING!\n");
		printf("Please take immediate action!\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void state_device(struct snapraid_state* state, int operation)
{
	tommy_node* i;
	unsigned j;
	tommy_list high;
	tommy_list low;

	switch (operation) {
	case DEVICE_UP : printf("Spinup...\n"); break;
	case DEVICE_DOWN : printf("Spindown...\n"); break;
	}

	tommy_list_init(&high);
	tommy_list_init(&low);

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = disk->device;
		pathcpy(entry->name, sizeof(entry->name), disk->name);
		pathcpy(entry->mount, sizeof(entry->mount), disk->dir);

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = state->parity[j].device;
		pathcpy(entry->name, sizeof(entry->name), lev_config_name(j));
		pathcpy(entry->mount, sizeof(entry->mount), state->parity[j].path);
		pathcut(entry->mount); /* remove the parity file */

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	if (devquery(&high, &low, operation) != 0) {
		const char* ope = 0;
		switch (operation) {
		case DEVICE_UP : ope = "Spinup"; break;
		case DEVICE_DOWN : ope = "Spindown"; break;
		case DEVICE_LIST : ope = "List"; break;
		case DEVICE_SMART : ope = "SMART"; break;
		}
		ferr("%s unsupported in this platform.\n", ope);
	} else {

#ifndef _WIN32
		if (operation == DEVICE_LIST) {
			for (i = tommy_list_head(&low); i != 0; i = i->next) {
				devinfo_t* devinfo = i->data;
				devinfo_t* parent = devinfo->parent;

				printf("%u:%u\t%s\t%u:%u\t%s\t%s\n", major(devinfo->device), minor(devinfo->device), devinfo->file, major(parent->device), minor(parent->device), parent->file, parent->name);
			}
		}
#endif

		if (operation == DEVICE_SMART)
			state_smart(state->opt.verbose, state->level + tommy_list_count(&state->disklist), &low);
	}

	tommy_list_foreach(&high, free);
	tommy_list_foreach(&low, free);
}

