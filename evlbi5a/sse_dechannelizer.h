/*
 * Copyright (c) 2010, 2011 Mark Kettenis
 * Copyright (c) 2010, 2011 Join Institute for VLBI in Europe
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef SSE_H
#define SSE_H

#if defined(__cplusplus)
extern "C" {
#endif

extern void extract_8Ch2bit1to2(void *src, void *dst0, void *dst1, void *dst2,
		void *dst3, void *dst4, void *dst5, void *dst6, void *dst7,
		size_t len) asm("extract_8Ch2bit1to2");
extern void extract_4Ch2bit1to2(void *src, void *dst0, void *dst1, void *dst2,
		void *dst3, size_t len) asm("extract_4Ch2bit1to2");
extern void extract_2Ch2bit1to2(void *src, void *dst0, void *dst1, size_t len) asm("extract_2Ch2bit1to2");

extern void extract_8Ch2bit(void *src, void *dst0, void *dst1, void *dst2,
		void *dst3, void *dst4, void *dst5, void *dst6, void *dst7,
		size_t len) asm("extract_8Ch2bit");

#if defined(__cplusplus)
}
#endif

#endif
