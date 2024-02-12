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

#ifndef JIVE5AB_SSE_DECHANNELIZER_H
#define JIVE5AB_SSE_DECHANNELIZER_H

/* For size_t */
#include <string.h>

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

extern void extract_16Ch2bit1to2(void *src, void *dst0, void *dst1, void *dst2,
		void *dst3, void *dst4, void *dst5, void *dst6, void *dst7,
		void *dst8, void *dst9, void *dst10, void *dst11, void *dst12,
		void *dst13, void *dst14, void *dst15,
		size_t len) asm("extract_16Ch2bit1to2");

/* NOTE: DIFFERENT CALL SEQUENCE! 
 *       src, len, dst0, dst1
 *       (fn's above have: "src, dst0, dst1, ... , dstN, len")
 */
extern void split8bitby4(void* src, size_t len, void* dst0, void* dst1, void* dst2, void* dst3) asm("split8bitby4");
extern void split8bitby4a(void* src, size_t len, void* dst0, void* dst1, void* dst2, void* dst3) asm("split8bitby4a");

extern void split16bitby2(void* src, size_t len, void* dst0, void* dst1) asm("split16bitby2");
extern void split16bitby4(void* src, size_t len, void* dst0, void* dst1, void* dst2, void* dst3) asm("split16bitby4");

extern void split32bitby2(void* src, size_t len, void* dst0, void* dst1) asm("split32bitby2");

extern void extract_16Ch2bit1to2_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7,
		void *dst8, void *dst9, void *dst10, void *dst11,
        void *dst12,void *dst13, void *dst14, void *dst15) asm("extract_16Ch2bit1to2_hv");
extern void extract_8Ch2bit1to2_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7 ) asm("extract_8Ch2bit1to2_hv");
extern void extract_8Ch2bit_hv(void *src, size_t len,
        void *dst0, void *dst1, void *dst2, void *dst3,
        void *dst4, void *dst5, void *dst6, void *dst7 ) asm("extract_8Ch2bit_hv");

/* This is not so much a splitter as it is a bitswapper -
 * changes standard astronomy Mark5B mode data sign/mag
 * bits into appropriate VDIF bitorder */
extern void swap_sign_mag(void* src, size_t len, void* dst0) asm("swap_sign_mag");


#if defined(__cplusplus)
}
#endif

#endif
