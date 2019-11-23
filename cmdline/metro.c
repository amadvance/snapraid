/*
 * Copyright (C) 2019 Andrea Mazzoleni
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

/*
 * Derivative work from metrohash128.cpp
 *
 * metrohash128.cpp
 *
 * Copyright 2015-2018 J. Andrew Rogers
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

static const uint64_t k0 = 0xC83A91E1;
static const uint64_t k1 = 0x8648DBDB;
static const uint64_t k2 = 0x7BDEC03B;
static const uint64_t k3 = 0x2F5870A5;

void MetroHash128(const void* data, size_t size, const uint8_t* seed, uint8_t* digest)
{
	const uint8_t* ptr = data;
	uint64_t v[4];

	v[0] = (util_read64(seed) - k0) * k3;
	v[1] = (util_read64(seed + 8) + k1) * k2;

	if (size >= 32) {
		v[2] = (util_read64(seed) + k0) * k2;
		v[3] = (util_read64(seed + 8) - k1) * k3;

		do {
			v[0] += util_read64(ptr) * k0; ptr += 8; v[0] = util_rotr64(v[0], 29) + v[2];
			v[1] += util_read64(ptr) * k1; ptr += 8; v[1] = util_rotr64(v[1], 29) + v[3];
			v[2] += util_read64(ptr) * k2; ptr += 8; v[2] = util_rotr64(v[2], 29) + v[0];
			v[3] += util_read64(ptr) * k3; ptr += 8; v[3] = util_rotr64(v[3], 29) + v[1];
			size -= 32;
		} while (size >= 32);

		v[2] ^= util_rotr64(((v[0] + v[3]) * k0) + v[1], 21) * k1;
		v[3] ^= util_rotr64(((v[1] + v[2]) * k1) + v[0], 21) * k0;
		v[0] ^= util_rotr64(((v[0] + v[2]) * k0) + v[3], 21) * k1;
		v[1] ^= util_rotr64(((v[1] + v[3]) * k1) + v[2], 21) * k0;
	}

	if (size >= 16) {
		v[0] += util_read64(ptr) * k2; ptr += 8; v[0] = util_rotr64(v[0], 33) * k3;
		v[1] += util_read64(ptr) * k2; ptr += 8; v[1] = util_rotr64(v[1], 33) * k3;
		v[0] ^= util_rotr64((v[0] * k2) + v[1], 45) * k1;
		v[1] ^= util_rotr64((v[1] * k3) + v[0], 45) * k0;
		size -= 16;
	}

	if (size >= 8) {
		v[0] += util_read64(ptr) * k2; ptr += 8; v[0] = util_rotr64(v[0], 33) * k3;
		v[0] ^= util_rotr64((v[0] * k2) + v[1], 27) * k1;
		size -= 8;
	}

	if (size >= 4) {
		v[1] += util_read32(ptr) * k2; ptr += 4; v[1] = util_rotr64(v[1], 33) * k3;
		v[1] ^= util_rotr64((v[1] * k3) + v[0], 46) * k0;
		size -= 4;
	}

	if (size >= 2) {
		v[0] += util_read16(ptr) * k2; ptr += 2; v[0] = util_rotr64(v[0], 33) * k3;
		v[0] ^= util_rotr64((v[0] * k2) + v[1], 22) * k1;
		size -= 2;
	}

	if (size >= 1) {
		v[1] += util_read8(ptr) * k2; v[1] = util_rotr64(v[1], 33) * k3;
		v[1] ^= util_rotr64((v[1] * k3) + v[0], 58) * k0;
	}

	v[0] += util_rotr64((v[0] * k0) + v[1], 13);
	v[1] += util_rotr64((v[1] * k1) + v[0], 37);
	v[0] += util_rotr64((v[0] * k2) + v[1], 13);
	v[1] += util_rotr64((v[1] * k3) + v[0], 37);

	util_write64(digest, v[0]);
	util_write64(digest + 8, v[0]);
}

