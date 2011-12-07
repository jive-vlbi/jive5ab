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
