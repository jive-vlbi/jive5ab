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

/*
 * Mark5A (Mark4/VLBA):
 *
 * These functions are named after the names used by SCHED for these
 * modes.  For example, extract_8Ch2bit1to2() extracts the individual
 * subbands from a mode with 8 subbands, 2-bit samples and a fan-out
 * of 2.
 *
 * So far the following functions are implemented:
 *
 * void extract_8Ch2bit1to2(void *src, void *dst0, void *dst1, void *dst2,
 *	void *dst3, void *dst4, void *dst5, void *dst6, void *dst7,
 *	size_t len);
 * void extract_4Ch2bit1to2(void *src, void *dst0, void *dst1, void *dst2,
 *	void *dst3, size_t len);
 * void extract_2Ch2bit1to2(void *src, void *dst0, void *dst1, size_t len);
 *
 * Mark5B:
 *
 * These functions have similar names to their Mark5A equivalents, but lack
 *
 * void extract_8Ch2bit(void *src, void *dst0, void *dst1, void *dst2,
 *	void *dst3, void *dst4, void *dst5, void *dst6, void *dst7,
 *	size_t len);
 *
 * All functions only use SSE2 instructions, so they should run on
 * everything since the Pentium 4.  The data to be converted is
 * specified by SRC, and the individual subbands will be stored into
 * DST0, DST1, DST2, etc.  LEN specifies the number of bytes to write
 * into each output buffer.  So the total number of input bytes
 * depends on the number of subbands in the mode.  For example for
 * '8Ch2bit1to2' the number of input bytes that will be converted is
 * 8 * LEN.
 *
 * The current implementation will actually read 16 bytes beyond the
 * end of the input buffer, so you'll have to allocate some extra
 * space to prevent a segmentation fault.
 *
 * Some additional performance could be gained if the input buffer can
 * be guaranteed to be 16-byte aligned.  Unfortunately on Linux
 * malloc(3), and therefore the C++ new operator, only guarantees tat
 * memory will be 8-byte aligned.
 */

	.globl	extract_8Ch2bit1to2
extract_8Ch2bit1to2:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi
	movl	44(%ebp), %ebx
	xorl	%ecx, %ecx

	movdqu	(%esi), %xmm0

1:
	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$15, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$13, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$11, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$9, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 1, samples in byte 0 and 8
	# Channel 3, samples in byte 2 and 10
	movdqa	%xmm1, %xmm7
	psrldq  $7, %xmm1
	por	%xmm1, %xmm7
	# Channel 1, samples in byte 0 and 1
	# Channel 3, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$7, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$5, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$3, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$1, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 2, samples in byte 0 and 8
	# Channel 4, samples in byte 2 and 10
	movdqa	%xmm1, %xmm6
	psrldq	$7, %xmm1
	por	%xmm1, %xmm6
	# Channel 2, samples in byte 0 and 1
	# Channel 4, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$14, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$12, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$10, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$8, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 5, samples in byte 0 and 8
	# Channel 7, samples in byte 2 and 10
	movdqa	%xmm1, %xmm5
	psrldq	$7, %xmm1
	por	%xmm1, %xmm5
	# Channel 5, samples in byte 0 and 1
	# Channel 7, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$6, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$4, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$2, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$0, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 6, samples in byte 5 and 13
	# Channel 8, samples in byte 7 and 15
	movdqa	%xmm1, %xmm4
	psrldq	$7, %xmm1
	por	%xmm1, %xmm4
	# Channel 6, samples in byte 0 and 1
	# Channel 8, samples in byte 2 and 3

	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	movl	12(%ebp), %edx
	movl	16(%ebp), %edi
	pextrw	$0, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm6, %eax
	movw	%ax, (%ecx, %edi)

	movl	20(%ebp), %edx
	movl	24(%ebp), %edi
	pextrw	$1, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$1, %xmm6, %eax
	movw	%ax, (%ecx, %edi)
	
	movl	28(%ebp), %edx
	movl	32(%ebp), %edi
	pextrw	$0, %xmm5, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm4, %eax
	movw	%ax, (%ecx, %edi)

	movl	36(%ebp), %edx
	movl	40(%ebp), %edi
	pextrw	$1, %xmm5, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$1, %xmm4, %eax
	movw	%ax, (%ecx, %edi)

	addl	$2, %ecx
	cmpl	%ecx, %ebx
	ja	1b

	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

	.globl	extract_4Ch2bit1to2
extract_4Ch2bit1to2:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi
	movl	28(%ebp), %ebx
	xorl	%ecx, %ecx

	movdqu	(%esi), %xmm0

1:
	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$15, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$14, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$13, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$12, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 1, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm7
	psrldq  $3, %xmm1
	por	%xmm1, %xmm7
	# Channel 1, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$11, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$10, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$9, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$8, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 2, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm6
	psrldq	$7, %xmm1
	por	%xmm1, %xmm6
	# Channel 2, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$7, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$6, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$5, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$4, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 3, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm5
	psrldq	$7, %xmm1
	por	%xmm1, %xmm5
	# Channel 3, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$3, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$2, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$1, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$0, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 4, samples in byte 0, 4, 8, 12
	movdqa	%xmm1, %xmm4
	psrldq	$7, %xmm1
	por	%xmm1, %xmm4
	# Channel 4, samples in byte 0, 1, 8 and 9

	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	movl	12(%ebp), %edx
	pextrw	$0, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm7, %eax
	movw	%ax, 2(%ecx, %edx)

	movl	16(%ebp), %edx
	pextrw	$0, %xmm6, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm6, %eax
	movw	%ax, 2(%ecx, %edx)
	
	movl	20(%ebp), %edx
	pextrw	$0, %xmm5, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm5, %eax
	movw	%ax, 2(%ecx, %edx)

	movl	24(%ebp), %edx
	pextrw	$0, %xmm4, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm4, %eax
	movw	%ax, (%ecx, %edx)

	addl	$4, %ecx
	cmpl	%ecx, %ebx
	ja	1b

	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

	.globl	extract_2Ch2bit1to2
extract_2Ch2bit1to2:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi
	movl	20(%ebp), %ebx
	xorl	%ecx, %ecx

	movdqu	(%esi), %xmm0

1:
	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$15, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$14, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$13, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$12, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Channel 1, samples in byte

	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 1, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm7
	psrldq  $3, %xmm1
	por	%xmm1, %xmm7
	# Channel 1, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$11, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$10, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$9, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$8, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 2, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm6
	psrldq	$7, %xmm1
	por	%xmm1, %xmm6
	# Channel 2, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$7, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$6, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$5, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$4, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 1, samples in byte 0, 4, 8 and 12
	movdqa	%xmm1, %xmm5
	psrldq	$7, %xmm1
	por	%xmm1, %xmm5
	# Channel 1, samples in byte 0, 1, 8 and 9

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$3, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$2, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$1, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$0, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 2, samples in byte 0, 4, 8, 12
	movdqa	%xmm1, %xmm4
	psrldq	$7, %xmm1
	por	%xmm1, %xmm4
	# Channel 2, samples in byte 0, 1, 8 and 9

	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	movl	12(%ebp), %edx
	pextrw	$0, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm7, %eax
	movw	%ax, 2(%ecx, %edx)

	movl	16(%ebp), %edx
	pextrw	$0, %xmm6, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$4, %xmm6, %eax
	movw	%ax, 2(%ecx, %edx)
	
	addl	$8, %ecx
	cmpl	%ecx, %ebx
	ja	1b

	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

	.globl	extract_8Ch2bit
extract_8Ch2bit:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi
	movl	44(%ebp), %ebx
	xorl	%ecx, %ecx

	movdqu	(%esi), %xmm0

1:
	# Isolate sign bit of channel 1
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$15, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 1
	psllw	$14, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm7
	psrldq  $7, %xmm1
	por	%xmm1, %xmm7

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 2
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$13, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 2
	psllw	$12, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm6
	psrldq  $7, %xmm1
	por	%xmm1, %xmm6

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 3
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$11, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 3
	psllw	$10, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm5
	psrldq  $7, %xmm1
	por	%xmm1, %xmm5

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 4
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$9, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 4
	psllw	$8, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm4
	psrldq  $7, %xmm1
	por	%xmm1, %xmm4

	# Samples in byte 0 and 1

	movl	12(%ebp), %edx
	movl	16(%ebp), %edi
	pextrw	$0, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm6, %eax
	movw	%ax, (%ecx, %edi)
	movl	20(%ebp), %edx
	movl	24(%ebp), %edi
	pextrw	$0, %xmm5, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm4, %eax
	movw	%ax, (%ecx, %edi)

	# Isolate sign bit of channel 5
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$7, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 5
	psllw	$6, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm7
	psrldq  $7, %xmm1
	por	%xmm1, %xmm7

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 6
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$5, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 6
	psllw	$4, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm6
	psrldq  $7, %xmm1
	por	%xmm1, %xmm6

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 7
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$3, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 7
	psllw	$2, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm5
	psrldq  $7, %xmm1
	por	%xmm1, %xmm5

	# Samples in byte 0 and 1

	# Isolate sign bit of channel 8
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	psllw	$1, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate mag bit of channel 8
	#psllw	$0, %xmm2
	psrlw	$15, %xmm2
	#psllw	$0, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0, 2, 4, 6, 8, 10, 12 and 16

	movdqa	%xmm1, %xmm2
	psrld	$16, %xmm2
	pslld	$2, %xmm2
	por	%xmm2, %xmm1

	# Samples in ow nibble byte 0, 4, 8 and 12

	movdqa	%xmm1, %xmm2
	psrlq	$32, %xmm2
	pslld	$4, %xmm2
	por	%xmm2, %xmm1

	# Samples in byte 0 and 8

	movdqa	%xmm1, %xmm4
	psrldq  $7, %xmm1
	por	%xmm1, %xmm4

	# Samples in byte 0 and 1

	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	movl	28(%ebp), %edx
	movl	32(%ebp), %edi
	pextrw	$0, %xmm7, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm6, %eax
	movw	%ax, (%ecx, %edi)

	movl	36(%ebp), %edx
	movl	40(%ebp), %edi
	pextrw	$0, %xmm5, %eax
	movw	%ax, (%ecx, %edx)
	pextrw	$0, %xmm4, %eax
	movw	%ax, (%ecx, %edi)

	addl	$2, %ecx
	cmpl	%ecx, %ebx
	ja	1b

	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

	.globl	extract_16Ch2bit1to2
extract_16Ch2bit1to2:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi
	movl	76(%ebp), %ebx
	xorl	%ecx, %ecx

	movdqu	(%esi), %xmm0
	.align	16
1:
	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$15, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$13, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$11, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$9, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 1, samples in byte 0 and 8
	# Channel 3, samples in byte 2 and 10
	movdqa	%xmm1, %xmm7
	psrldq  $7, %xmm1
	por	%xmm1, %xmm7
	# Channel 1, samples in byte 0 and 1
	# Channel 3, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$7, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$5, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$3, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$1, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 2, samples in byte 0 and 8
	# Channel 4, samples in byte 2 and 10
	movdqa	%xmm1, %xmm6
	psrldq	$7, %xmm1
	por	%xmm1, %xmm6
	# Channel 2, samples in byte 0 and 1
	# Channel 4, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$14, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$12, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$10, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$8, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 5, samples in byte 0 and 8
	# Channel 7, samples in byte 2 and 10
	movdqa	%xmm1, %xmm5
	psrldq	$7, %xmm1
	por	%xmm1, %xmm5
	# Channel 5, samples in byte 0 and 1
	# Channel 7, samples in byte 2 and 3

	# Isolate first sign bit
	movdqa	%xmm0, %xmm1
	movdqa	%xmm0, %xmm2
	movdqa	%xmm0, %xmm3
	movdqa	%xmm0, %xmm4
	psllw	$6, %xmm1
	psrlw	$15, %xmm1
	psllw	$1, %xmm1
	# Isolate second sign bit
	psllw	$4, %xmm2
	psrlw	$15, %xmm2
	psllw	$3, %xmm2
	por	%xmm2, %xmm1
	# Isolate first mag bit
	psllw	$2, %xmm3
	psrlw	$15, %xmm3
	psllw	$0, %xmm3
	por	%xmm3, %xmm1
	# Isolate second mag bit
	psllw	$0, %xmm4
	psrlw	$15, %xmm4
	psllw	$2, %xmm4
	por	%xmm4, %xmm1
	# Merge next nibble
	movdqa	%xmm1, %xmm2
	psllq	$32, %xmm1
	psrlq	$32, %xmm1
	psrlq	$32, %xmm2
	psllq	$4, %xmm2
	por	%xmm2, %xmm1
	# Channel 6, samples in byte 5 and 13
	# Channel 8, samples in byte 7 and 15
	movdqa	%xmm1, %xmm4
	psrldq	$7, %xmm1
	por	%xmm1, %xmm4
	# Channel 6, samples in byte 0 and 1
	# Channel 8, samples in byte 2 and 3

	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	movl	12(%ebp), %edx
	movl	44(%ebp), %edi
	pextrw	$0, %xmm7, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)
	
	movl	16(%ebp), %edx
	movl	48(%ebp), %edi
	pextrw	$0, %xmm6, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	movl	20(%ebp), %edx
	movl	52(%ebp), %edi
	pextrw	$1, %xmm7, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	movl	24(%ebp), %edx
	movl	56(%ebp), %edi
	pextrw	$1, %xmm6, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)
	
	movl	28(%ebp), %edx
	movl	60(%ebp), %edi
	pextrw	$0, %xmm5, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	movl	32(%ebp), %edx
	movl	64(%ebp), %edi
	pextrw	$0, %xmm4, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	movl	36(%ebp), %edx
	movl	68(%ebp), %edi
	pextrw	$1, %xmm5, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	movl	40(%ebp), %edx
	movl	72(%ebp), %edi
	pextrw	$1, %xmm4, %eax
	movb	%al, (%ecx, %edx)
	movb	%ah, (%ecx, %edi)

	addl	$1, %ecx
	cmpl	%ecx, %ebx
	ja	1b

	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

.globl split16bitby2
split16bitby2:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%edi
	movl	20(%ebp), %ecx  /* dst1 */
	pushl	%esi
	movl	16(%ebp), %edi  /* dst0 */
	movl	12(%ebp), %esi  /* len */
	movl	8(%ebp), %edx   /* src */
    jmp     L2
L6:
    movdqu  (%edx), %xmm0
    pshuflw $216, %xmm0, %xmm1
    pshufhw $216, %xmm1, %xmm0
    pshufd  $216, %xmm0, %xmm1
    movq    %xmm1, (%edi)
    psrldq  $8, %xmm1
    movq    %xmm1, (%ecx)
    addl    $16, %edx
    addl    $8,  %edi
    addl    $8,  %ecx
	subl	$16, %esi
L2:
	cmpl	$15, %esi
	ja	L6
	popl	%esi
	popl	%edi
	leave
	ret

.globl split8bitby4a
split8bitby4a:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%edi
	pushl	%esi
    pushl   %ebx

    /* Load function arguments */
    /*  8(%ebp)  == src  ebx*/
    movl    8(%ebp), %ebx
    /*  12(%ebp) == len ecx */
    movl    12(%ebp), %ecx
    shrl    $2, %ecx
    /*  16(%ebp) == d0  ebx */
    /*  20(%ebp) == d1  edx */
    movl    20(%ebp), %edx
    /*  24(%ebp) == d2  esi */
    movl    20(%ebp), %esi
    /*  28(%ebp) == d3  edi*/
    movl    20(%ebp), %edi
    jmp     L2b
L6b:
    movl    (%ebx), %eax
    movb    %al, (%edi)
    movb    %ah, (%esi)
    shrl    $16, %eax
    movb    %al, (%edx)
    /* before writing back the last byte,
       update src (%ebx) and exchange with d0 */
    addl    $4, %ebx
    xchgl   %ebx,16(%ebp)
    movb    %ah, (%ebx)

    incl    %edi
    incl    %esi
    incl    %edx
    incl    %ebx
    subl    $4, %ecx
    /* and switch d0 <-> src back again */
    xchgl   %ebx,16(%ebp)
L2b:
	cmpl	$3, %ecx
	ja	L6b

    popl    %ebx
	popl	%esi
	popl	%edi
	leave
	ret

/* This functions will read 16 bytes past the memory 
   block [src, src+size] (first + second arg) */
#if SSE>40
.globl split8bitby4
split8bitby4:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
	pushl	%edi
	pushl	%ebx

    /* Make room for the 16-byte shuffling mask */
    subl    $16, %esp

    /* Load 1 128 bit shuffle mask in 4x32bit steps masks onto the stack,
       such that the bytes come out as
       0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15
       so we can get them out easily */
    movl    $0x0f0b0703, 12(%esp)
    movl    $0x0e0a0602, 8(%esp)
    movl    $0x0d090501, 4(%esp)
    movl    $0x0c080400, 0(%esp)

    movdqu  (%esp), %xmm7
    addl    $16, %esp

    /* Load function arguments */
    /*  8(%ebp)  == src */
    /*  12(%ebp) == len */
    /*  16(%ebp) == d0 */
    /*  20(%ebp) == d1 */
    /*  24(%ebp) == d2 */
    /*  28(%ebp) == d3 */

    /* esi == src */
	movl	8(%ebp), %esi 

    /* initiate read of 16 bytes into xmm1 */
    movdqu  (%esi), %xmm1

    /* modify length such that it holds the
       value of how often we can execute the 16-byte loop */
    movl    12(%ebp), %ecx
    shr     $4, %ecx
    /* ecx will count how often we've gone through the loop
       (to compute the addresses) */

    /* can use eax, ebx, edx and edi to cache d0 .. d3
       so they won't have to be loaded from the 
       stack (cache) all the time. */
    movl    16(%ebp), %eax
    movl    20(%ebp), %ebx
    movl    24(%ebp), %edx
    movl    28(%ebp), %edi

    jmp     L3a
L7a:
    /* copy data into xmm0 - the next read will be initiated
       somewhere below */
    movdqa  %xmm1, %xmm0

    /* xmm7 has the shuffling mask */
    pshufb  %xmm7, %xmm0

    /* and copy out the 4*4bytes */
    /* pextrd is only available in SSE4.1 */
    pextrd  $0, %xmm0, (%eax)
    pextrd  $1, %xmm0, (%ebx)
    pextrd  $2, %xmm0, (%edx)
    pextrd  $3, %xmm0, (%edi)
    /* already start reading the next word into xmm1 */
    addl    $16, %esi
    movdqu  (%esi), %xmm1

    /* update loopvariables */
    decl    %ecx
    addl    $4, %eax
    addl    $4, %ebx
    addl    $4, %edx
    addl    $4, %edi
L3a:
	cmpl	$0, %ecx
	ja	    L7a

	popl	%ebx
	popl	%edi
	popl	%esi

    leave 
	ret

#else /* SSE4.1*/

/* This should work with SSE2 */
.globl split8bitby4
split8bitby4:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
	pushl	%edi
	pushl	%ebx

    /* Load function arguments */
    /*  8(%ebp)  == src */
    /*  12(%ebp) == len */
    /*  16(%ebp) == d0 */
    /*  20(%ebp) == d1 */
    /*  24(%ebp) == d2 */
    /*  28(%ebp) == d3 */

    /* esi == src */
	movl	8(%ebp), %esi 

    /* initiate read of 16 bytes into xmm1 */
    movdqu  (%esi), %xmm0

    /* ecx counts from 0 -> n  loops */
    /* we first modify our len argument to truncate it
       to N 16-byte loops */
    movl    12(%ebp), %ecx
    shrl    $4, %ecx
    movl    %ecx, 12(%ebp)
    xorl    %ecx, %ecx

    jmp     L3a
L7a:
    /* copy data into xmm0 - the next read will be initiated
       somewhere below */
    movdqa  %xmm0, %xmm1

    /* go through this byte by byte _sigh_
       sse2 doesn't have byte-shuffling */
    /* reshuffle by 16 bits such that we have
       the even words followed by the odd words,
       then we can go through them two destinations
       at a time */
    /* we have:
        src =  0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
       we want:
        d0 = 0 4 8 12
        d1 = 1 5 9 13
        d2 = 2 6 10 14
        d3 = 3 7 11 15
        */
             
    pshuflw $216, %xmm1, %xmm2
    pshufhw $216, %xmm2, %xmm1
    pshufd  $216, %xmm1, %xmm2
    /* The order of the bytes now is:
       0 1 4 5 8 9 12 13 2 3 6 7 10 11 14 15
    
       then move 32 bits to eax and copy to ebx:
       eax 0 1 4 5
       ebx 0 1 4 5

       shift ebx by 16 bits
       eax 0 1 4 5
       ebx 4 5 0 0

       exchange the 1 and the 4
       eax 0 4 4 5
       ebx 1 5 0 0

       and move ax to destination 0
                bx ,,     ,,      1 
     */

    /* Process the first 8 bytes (2 times 4 chunks) - they
       go to d0 + d1 */
    /* load the two desinations */
    movl    16(%ebp), %edx
    movl    20(%ebp), %edi
    movd    %xmm2, %eax
    movl    %eax, %ebx
    shrl    $16, %ebx
    xchgb   %ah, %bl
    movw    %ax, (%edx, %ecx, 4)
    movw    %bx, (%edi, %ecx, 4)

    psrldq  $4, %xmm2
    movd    %xmm2, %eax
    movl    %eax, %ebx
    shrl    $16, %ebx
    xchgb   %ah, %bl
    movw    %ax, 2(%edx, %ecx, 4)
    movw    %bx, 2(%edi, %ecx, 4)

    /* Remaining 8 bytes go to d2 + d3 */
    movl    24(%ebp), %edx
    movl    28(%ebp), %edi

    psrldq  $4, %xmm2
    movd    %xmm2, %eax
    movl    %eax, %ebx
    shrl    $16, %ebx
    xchgb   %ah, %bl
    movw    %ax, 0(%edx, %ecx, 4)
    movw    %bx, 0(%edi, %ecx, 4)

    psrldq  $4, %xmm2
    movd    %xmm2, %eax
    movl    %eax, %ebx
    shrl    $16, %ebx
    xchgb   %ah, %bl
    movw    %ax, 2(%edx, %ecx, 4)
    movw    %bx, 2(%edi, %ecx, 4)

    /* already start reading the next word into xmm1 */
    addl    $16, %esi
    movdqu  (%esi), %xmm0

    /* update loopvariables */
    incl    %ecx
L3a:
	cmpl	12(%ebp), %ecx
	jb	    L7a

	popl	%ebx
	popl	%edi
	popl	%esi

    leave 
	ret
#endif

    .globl split8bitby4_old
split8bitby4_old:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
	pushl	%edi
	pushl	%ebx

    /* Load the shuffle masks into xmm4-7,
       see below */
    movl    $0x0c080400, %ebx
    movd    %ebx, %xmm4
    movl    $0x0d090501, %ebx
    movd    %ebx, %xmm5
    movl    $0x0e0a0602, %ebx
    movd    %ebx, %xmm6
    movl    $0x0f0b0703, %ebx
    movd    %ebx, %xmm7
    /* Load function arguments */
    /*  8(%ebp)  == src */
    /*  12(%ebp) == len */
    /*  16(%ebp) == d0 */
    /*  20(%ebp) == d1 */
    /*  24(%ebp) == d2 */
    /*  28(%ebp) == d3 */
	movl	8(%ebp), %esi 
    movl    12(%ebp), %edi
    /* %ecx is the loop counter, init to 0 */
    xorl    %ecx, %ecx
    jmp     L3
L7:
    /* 16 bytes or src data in xmm0 */
    movdqu  (%esi), %xmm0
    /* before shuffling (it will ruin contents)
       create 4 copies. In each copy rearrange:
       xmm0: 0,4,8,12
       xmm1: 1,5,9,13
       xmm2: 2,6,10,14
       xmm3: 3,7,11,15  */
    movdqa  %xmm0, %xmm1
    movdqa  %xmm0, %xmm2
    movdqa  %xmm0, %xmm3
    pshufb  %xmm4, %xmm0
    pshufb  %xmm5, %xmm1
    pshufb  %xmm6, %xmm2
    pshufb  %xmm7, %xmm3
    /* and copy out the 4*4bytes */
	movl	16(%ebp), %edx
    movd    %xmm0, (%edx,%ecx,4)
	movl	20(%ebp), %edx
    movd    %xmm1, (%edx,%ecx,4)
	movl	24(%ebp), %edx
    movd    %xmm2, (%edx,%ecx,4)
	movl	28(%ebp), %edx
    movd    %xmm3, (%edx,%ecx,4)
    /* update loopvariables */
    addl    $16, %esi
    incl    %ecx
	subl	$16, %edi
L3:
	cmpl	$15, %edi
	ja	L7

	popl	%ebx
	popl	%edi
	popl	%esi

    leave 
	ret
