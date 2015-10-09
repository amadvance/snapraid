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
static unsigned SMART_5_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 5 at a given value.
 */
static double SMART_5_R[SMART_MEASURES] = {
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
static unsigned SMART_187_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 187 at a given value.
 */
static double SMART_187_R[SMART_MEASURES] = {
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
static unsigned SMART_188_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 188 at a given value.
 */
static double SMART_188_R[SMART_MEASURES] = {
	0.0022, 0.0087, 0.0113, 0.0131, 0.0143,
	0.0157, 0.0176, 0.0203, 0.0233, 0.0271,
	0.0317, 0.0377, 0.0449, 0.0542, 0.0658,
	0.0798, 0.0934, 0.1100, 0.1268, 0.1451,
	0.1626, 0.1820, 0.1975, 0.2138, 0.2270,
	0.2397, 0.2515, 0.2606, 0.2692, 0.2792,
	0.2861, 0.2954, 0.3050, 0.3165, 0.3252,
	0.3369, 0.3430, 0.3512, 0.3588, 0.3731,
	0.3777, 0.3894, 0.3981, 0.4148, 0.4206,
	0.4325, 0.4354, 0.4449, 0.4503, 0.4635,
	0.4695, 0.4806, 0.4871, 0.4988, 0.5007,
	0.5099, 0.5144, 0.5270, 0.5313, 0.5393,
	0.5456, 0.5529, 0.5542, 0.5633, 0.5657,
	0.5729, 0.5748, 0.5814, 0.5906, 0.5977,
	0.6045, 0.6096, 0.6208, 0.6259, 0.6293,
	0.6347, 0.6354, 0.6389, 0.6449, 0.6481,
	0.6549, 0.6581, 0.6604, 0.6703, 0.6759,
	0.6830, 0.6882, 0.6900, 0.6931, 0.7027,
	0.7070, 0.7125, 0.7202, 0.7302, 0.7405,
	0.7467, 0.7520, 0.7573, 0.7616, 0.7688,
	0.7732, 0.7773, 0.7825, 0.7890, 0.7940,
	0.8113, 0.8213, 0.8344, 0.8364, 0.8403,
	0.8449, 0.8488, 0.8567, 0.8621, 0.8668,
	0.8729, 0.8737, 0.8750, 0.8782, 0.8807,
	0.8821, 0.8920, 0.8987, 0.8990, 0.9099,
	0.9141, 0.9165, 0.9195, 0.9198, 0.9235,
	0.9247, 0.9315, 0.9359, 0.9372, 0.9394,
	0.9451, 0.9454, 0.9493, 0.9520, 0.9534,
	0.9556, 0.9564, 0.9605, 0.9628, 0.9648,
	0.9709, 0.9717, 0.9761, 0.9784, 0.9788,
	0.9824, 0.9884, 0.9938, 0.9944, 1.0056,
	1.0061, 1.0111, 1.0314, 1.0375, 1.0393,
	1.0396, 1.0397, 1.0414, 1.0418, 1.0466,
	1.0467, 1.0474, 1.0475, 1.0523, 1.0523,
	1.0524, 1.0525, 1.0557, 1.0597, 1.0597,
	1.0598, 1.0599, 1.0599, 1.0602, 1.0603,
	1.0603, 1.0605, 1.0605, 1.0610, 1.0611,
	1.0613, 1.0615, 1.0628, 1.0666, 1.0732,
	1.0792, 1.0796, 1.0797, 1.0864, 1.0935,
	1.0936, 1.1013, 1.1066, 1.1088, 1.1088,
	1.1093, 1.1119, 1.1136, 1.1146, 1.1245,
	1.1274, 1.1275, 1.1276, 1.1280, 1.1280,
	1.1347, 1.1348, 1.1348, 1.1348, 1.1350,
	1.1408, 1.1476, 1.1477, 1.1477, 1.1478,
	1.1479, 1.1479, 1.1479, 1.1480, 1.1480,
	1.1564, 1.1714, 1.1732, 1.1747, 1.1816,
	1.1819, 1.1887, 1.1906, 1.2126, 1.2167,
	1.2168, 1.2168, 1.2168, 1.2171, 1.2171,
	1.2268, 1.2396, 1.2397, 1.2418, 1.2478,
	1.2588, 1.2610, 1.2615, 1.2651, 1.2668,
	1.2682, 1.2732, 1.2738, 1.2747, 1.2764,
	1.2765,
};

/*
 * Divider for SMART attribute 193
 */
static unsigned SMART_193_STEP = 649;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 193 at a given value.
 */
static double SMART_193_R[SMART_MEASURES] = {
	0.0000, 0.0014, 0.0027, 0.0030, 0.0031,
	0.0033, 0.0036, 0.0038, 0.0041, 0.0043,
	0.0046, 0.0048, 0.0050, 0.0053, 0.0056,
	0.0058, 0.0060, 0.0064, 0.0067, 0.0069,
	0.0073, 0.0076, 0.0079, 0.0082, 0.0085,
	0.0088, 0.0091, 0.0094, 0.0097, 0.0100,
	0.0104, 0.0107, 0.0110, 0.0113, 0.0115,
	0.0117, 0.0120, 0.0123, 0.0126, 0.0129,
	0.0131, 0.0134, 0.0138, 0.0141, 0.0144,
	0.0149, 0.0153, 0.0156, 0.0159, 0.0162,
	0.0166, 0.0168, 0.0171, 0.0173, 0.0176,
	0.0179, 0.0183, 0.0186, 0.0191, 0.0196,
	0.0202, 0.0207, 0.0213, 0.0217, 0.0221,
	0.0225, 0.0233, 0.0238, 0.0243, 0.0247,
	0.0252, 0.0257, 0.0261, 0.0265, 0.0271,
	0.0278, 0.0284, 0.0290, 0.0294, 0.0298,
	0.0304, 0.0310, 0.0316, 0.0321, 0.0325,
	0.0329, 0.0334, 0.0340, 0.0346, 0.0351,
	0.0355, 0.0359, 0.0365, 0.0372, 0.0379,
	0.0387, 0.0395, 0.0405, 0.0413, 0.0419,
	0.0424, 0.0432, 0.0440, 0.0447, 0.0455,
	0.0461, 0.0467, 0.0471, 0.0476, 0.0481,
	0.0486, 0.0490, 0.0495, 0.0500, 0.0505,
	0.0512, 0.0518, 0.0524, 0.0532, 0.0540,
	0.0545, 0.0552, 0.0559, 0.0566, 0.0574,
	0.0582, 0.0592, 0.0601, 0.0607, 0.0616,
	0.0623, 0.0631, 0.0639, 0.0646, 0.0654,
	0.0661, 0.0668, 0.0677, 0.0685, 0.0696,
	0.0707, 0.0717, 0.0727, 0.0737, 0.0749,
	0.0759, 0.0773, 0.0783, 0.0791, 0.0800,
	0.0809, 0.0819, 0.0829, 0.0839, 0.0847,
	0.0858, 0.0867, 0.0876, 0.0884, 0.0895,
	0.0906, 0.0919, 0.0931, 0.0943, 0.0954,
	0.0966, 0.0976, 0.0985, 0.0995, 0.1009,
	0.1021, 0.1038, 0.1051, 0.1065, 0.1076,
	0.1090, 0.1106, 0.1119, 0.1130, 0.1146,
	0.1158, 0.1168, 0.1181, 0.1193, 0.1206,
	0.1216, 0.1227, 0.1241, 0.1257, 0.1268,
	0.1277, 0.1289, 0.1302, 0.1316, 0.1329,
	0.1341, 0.1355, 0.1372, 0.1387, 0.1400,
	0.1415, 0.1427, 0.1438, 0.1447, 0.1458,
	0.1470, 0.1480, 0.1491, 0.1502, 0.1517,
	0.1528, 0.1539, 0.1555, 0.1568, 0.1583,
	0.1594, 0.1606, 0.1618, 0.1635, 0.1649,
	0.1665, 0.1686, 0.1700, 0.1719, 0.1731,
	0.1743, 0.1755, 0.1777, 0.1788, 0.1802,
	0.1812, 0.1824, 0.1835, 0.1848, 0.1859,
	0.1869, 0.1880, 0.1890, 0.1901, 0.1913,
	0.1926, 0.1935, 0.1948, 0.1960, 0.1977,
	0.1989, 0.2001, 0.2013, 0.2024, 0.2036,
	0.2049, 0.2062, 0.2074, 0.2087, 0.2099,
	0.2111,
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
static unsigned SMART_198_STEP = 1;

/*
 * Failure rate for 30 days, for a disk
 * with SMART attribute 198 at a given value.
 */
static double SMART_198_R[SMART_MEASURES] = {
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
static double smart_afr(uint64_t* smart)
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

	if (smart[188] != SMART_UNASSIGNED) {
		/* with Seagate disks, there are three different 16 bits value reported */
		/* the lowest one is the most significative */
		double r = smart_afr_value(SMART_188_R, SMART_188_STEP, smart[188] & mask16);
		if (afr < r)
			afr = r;
	}

	if (smart[193] != SMART_UNASSIGNED) {
		double r = smart_afr_value(SMART_193_R, SMART_193_STEP, smart[193] & mask32);
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
 * Print a string with space padding.
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
 * Print a probability with space padding.
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

static void state_smart(unsigned n, tommy_list* low)
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
			printf("%7" PRIu64, devinfo->smart[194] & mask16);
		else if (devinfo->smart[190] != SMART_UNASSIGNED)
			printf("%7" PRIu64, devinfo->smart[190] & mask16);
		else
			printf("      -");

		if (devinfo->smart[9] != SMART_UNASSIGNED)
			printf("%7" PRIu64, (devinfo->smart[9] & mask32) / 24);
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
		} else if (devinfo->smart[SMART_ROTATION_RATE] == 0) {
			/* if SSD, skip AFR estimation as data is from not SSD disks */
			afr = 0;
			printf("  SSD");
		} else {
			afr = smart_afr(devinfo->smart);

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

		if (devinfo->smart[SMART_SIZE] != SMART_UNASSIGNED)
			printf(" %4.1f", devinfo->smart[SMART_SIZE] / 1E12);
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

		log_tag("smart:%s:%s\n", devinfo->file, devinfo->name);
		if (devinfo->smart_serial[0])
			log_tag("attr:%s:%s:serial:%s\n", devinfo->file, devinfo->name, esc(devinfo->smart_serial));
		if (afr != 0)
			log_tag("attr:%s:%s:afr:%g\n", devinfo->file, devinfo->name, afr);
		if (devinfo->smart[SMART_SIZE] != SMART_UNASSIGNED)
			log_tag("attr:%s:%s:size:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_SIZE]);
		if (devinfo->smart[SMART_ERROR] != SMART_UNASSIGNED)
			log_tag("attr:%s:%s:error:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_ERROR]);
		if (devinfo->smart[SMART_ROTATION_RATE] != SMART_UNASSIGNED)
			log_tag("attr:%s:%s:rotationrate:%" PRIu64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_ROTATION_RATE]);
		if (devinfo->smart[SMART_FLAGS] != SMART_UNASSIGNED)
			log_tag("attr:%s:%s:flags:%" PRIu64 ":%" PRIx64 "\n", devinfo->file, devinfo->name, devinfo->smart[SMART_FLAGS], devinfo->smart[SMART_FLAGS]);
		for (j = 0; j < 256; ++j)
			if (devinfo->smart[j] != SMART_UNASSIGNED)
				log_tag("attr:%s:%s:%u:%" PRIu64 ":%" PRIx64 "\n", devinfo->file, devinfo->name, j, devinfo->smart[j], devinfo->smart[j]);
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
	printf("sequence of failures that the parity WONT be able to recover, assuming\n");
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

/**
 * Fill with fake data the device list.
 */
static int devtest(tommy_list* low, int operation)
{
	unsigned c;

	if (operation != DEVICE_SMART)
		return -1;

	/* add some fake data */
	for (c = 0; c < 16; ++c) {
		devinfo_t* entry;
		int j;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = c;

		tommy_list_insert_tail(low, &entry->node, entry);

		for (j = 0; j < 256; ++j) {
			switch (c) {
			case 0 : entry->smart[j] = 0; break;
			case 1 : entry->smart[j] = SMART_UNASSIGNED; break;
			default:
				if (j == 193)
					entry->smart[j] = c - 2;
				else
					entry->smart[j] = 0;
				break;
			}
		}

		if (c == 0) {
			entry->smart_serial[0] = 0;
			entry->file[0] = 0;
			entry->name[0] = 0;
			entry->smart[SMART_SIZE] = SMART_UNASSIGNED;
			entry->smart[SMART_ROTATION_RATE] = 0;
		} else {
			snprintf(entry->smart_serial, sizeof(entry->smart_serial), "%u", c);
			pathcpy(entry->file, sizeof(entry->name), "file");
			pathcpy(entry->name, sizeof(entry->name), "name");
			entry->smart[SMART_SIZE] = c * TERA;
			entry->smart[SMART_ROTATION_RATE] = 7200;
		}

		entry->smart[SMART_ERROR] = 0;
		entry->smart[SMART_FLAGS] = SMART_UNASSIGNED;

		switch (c) {
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
	}

	return 0;
}

void state_device(struct snapraid_state* state, int operation)
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

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = disk->device;
		pathcpy(entry->name, sizeof(entry->name), disk->name);
		pathcpy(entry->mount, sizeof(entry->mount), disk->dir);
		pathcpy(entry->smartctl, sizeof(entry->smartctl), disk->smartctl);

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = state->parity[j].device;
		pathcpy(entry->name, sizeof(entry->name), lev_config_name(j));
		pathcpy(entry->mount, sizeof(entry->mount), state->parity[j].path);
		pathcpy(entry->smartctl, sizeof(entry->smartctl), state->parity[j].smartctl);
		pathcut(entry->mount); /* remove the parity file */

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	if (state->opt.fake_device)
		ret = devtest(&low, operation);
	else
		ret = devquery(&high, &low, operation);

	if (ret != 0) {
		const char* ope = 0;
		switch (operation) {
		case DEVICE_UP : ope = "Spinup"; break;
		case DEVICE_DOWN : ope = "Spindown"; break;
		case DEVICE_LIST : ope = "List"; break;
		case DEVICE_SMART : ope = "SMART"; break;
		}
		log_fatal("%s unsupported in this platform.\n", ope);
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
			state_smart(state->level + tommy_list_count(&state->disklist), &low);
	}

	tommy_list_foreach(&high, free);
	tommy_list_foreach(&low, free);
}

