/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifndef RETAIL_COMPATIBLE_CRC
#define RETAIL_COMPATIBLE_CRC (1) // Game is expected to be CRC compatible with retail Generals 1.08, Zero Hour 1.04
#endif

#ifndef USE_DETERMINISTIC_MATH
#define USE_DETERMINISTIC_MATH (1) // Game uses deterministic math for game simulation compatibility among different system architectures in peer to peer networks
#endif

#if defined(__has_include)
#if __has_include("gmath.h")
#define HAS_GAMEMATH (1)
#endif
#endif

// Contra: deterministic math is used even though the rest of the game stays retail compatible. Our 32-bit and 64-bit
// builds must compute the game simulation bit for bit alike, which the math functions of the 32-bit and the 64-bit
// MSVC runtime do not. This gives up compatibility with replays and games of retail and of Contra 1.3 and older.
#if !HAS_GAMEMATH
#undef USE_DETERMINISTIC_MATH // Unfortunately cannot actually use deterministic math
#endif
