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

	.globl	extract_8Ch2bit1to2_hv
extract_8Ch2bit1to2_hv:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi    # src
	xorl	%ecx, %ecx       # cnt

    # divide length of block by eight -
    # we split in 8 pieces!
    #  12(%ebp) == len 
    movl    12(%ebp), %ebx
    shrl    $3, %ebx
    movl    %ebx, 12(%ebp)

    # immediately issue the first read
	movdqu	(%esi), %xmm0

    # free: eax, ebx, edx, edi

    # Ch0 = bits 0 2 4 6
    # Ch1 = bits 1 3 5 7
    # Ch2 = bits 8 10 12 14
    # etc
    # make room on the stack for the masks
    # edi = baseaddress for the 16-byte aligned masks
    subl    $160, %esp
    movl    %esp, %edi
    addl    $31, %edi
    andl    $0xfffffff0, %edi   # 16-byte aligned mask base

    # each byte has four bits (two samples) of a channel
    # we can easily rearrange them so each channel ends
    # up in a nibble. Later rearranging the nibbles 
    # will quickly give us a byte of each channel

    # ch a, mag [and c, e, g]
    movl    $0x50505050, %eax
    movl    %eax, 0(%edi)
    movl    %eax, 4(%edi)
    movl    %eax, 8(%edi)
    movl    %eax, 12(%edi)

    # ch a, sign [....]
    movl    $0x05050505, %eax
    movl    %eax, 16(%edi)
    movl    %eax, 20(%edi)
    movl    %eax, 24(%edi)
    movl    %eax, 28(%edi)

    # ch b, mag [and d,f,h]
    movl    $0xa0a0a0a0, %eax
    movl    %eax, 32(%edi)
    movl    %eax, 36(%edi)
    movl    %eax, 40(%edi)
    movl    %eax, 44(%edi)

    # ch b, sign [...]
    movl    $0x0a0a0a0a, %eax
    movl    %eax, 48(%edi)
    movl    %eax, 52(%edi)
    movl    %eax, 56(%edi)
    movl    %eax, 60(%edi)


    # free registers eax, ebx, edx

	.align	16
1:
    # bitmasks 
    movdqa  0(%edi),   %xmm7   # ch a, mag
    movdqa  16(%edi),  %xmm6   # ch a, sgn
    movdqa  32(%edi),  %xmm5   # ch b, mag
    movdqa  48(%edi),  %xmm4   # ch b, sgn

    # isolate the bits
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6
    pand    %xmm0, %xmm5
    pand    %xmm0, %xmm4

    # shift bits to where they should go
    psrld   $4, %xmm7
    pslld   $1, %xmm6 
    psrld   $1, %xmm5
    pslld   $4, %xmm4

    # we need to merge the two half nibbles
    por     %xmm6, %xmm7
    por     %xmm4, %xmm5

    # xmm7 has lo nibbles filled with ch a,c,e,g
    # xmm5 has hi nibbles filled with ch b,d,f,h

    # create copies so we can shift + merge two nibbles to one byte
    movdqa  %xmm7, %xmm6
    movdqa  %xmm5, %xmm4

    # for ch a: the next nibble is found 32 bits ahead. move to front
    # then move the bits 4 bits, to the hi nibble, ie 28 bits right in total
    psrlq   $28, %xmm6

    # ch b: the nibble in the second 32 bits is already in the correct
    # location (the hi nibble). move the data 28 bits to the left and
    # the first nibble should line up just fine
    psllq   $28, %xmm5

    # merge
    por %xmm6, %xmm7    # bytes in xmm7, dword 1, 3 [1-based counting]
    por %xmm5, %xmm4    # bytes in xmm4, dword 2, 4 [..]

    # now we have a lot of single bytes - lets try to merge them
    # before writing them out
    # use the unpack+interleave routines. copy the original data
    # move the high quadword to the front, unpack+interleave the
    # low quadword

    # before copy/shuffle, bring all of xmm4 32 bits to the right
    # such that the first bit is actually at bit 0
    psrldq   $4, %xmm4

    # we really only need dword 3 [1-based counting] from xmm7
    #  and dword 4 from xmm4
    pshufd  $2, %xmm7, %xmm6
    pshufd  $2, %xmm4, %xmm3

    # now we can interleave the low order bytes from xmm7+xmm6
    # and xmm4+xmm3
    punpcklbw   %xmm6, %xmm7
    punpcklbw   %xmm3, %xmm4
   
    # Now we should have the words (ie 16bits) per channel
    # organized as:
    #  register     word     ch
    #    xmm7       0        a
    #    xmm7       1        c
    #    xmm7       2        e
    #    xmm4       0        b
    #        etc
	movl	16(%ebp), %ebx #0
	pextrw	$0, %xmm7, %eax
	movl	24(%ebp), %edx #2
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm7, %eax
    movw    %ax, (%ecx,%edx) 

	movl	32(%ebp), %ebx #4
	pextrw	$2, %xmm7, %eax
	movl	40(%ebp), %edx #6
    movw    %ax, (%ecx,%ebx) 
	pextrw	$3, %xmm7, %eax
    movw    %ax, (%ecx,%edx) 

	movl	20(%ebp), %ebx #1
	pextrw	$0, %xmm4, %eax
	movl	28(%ebp), %edx #3
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm4, %eax
    movw    %ax, (%ecx,%edx) 

	movl	36(%ebp), %ebx #5
	pextrw	$2, %xmm4, %eax
	movl	44(%ebp), %edx #7
    movw    %ax, (%ecx,%ebx) 
	pextrw	$3, %xmm4, %eax
    movw    %ax, (%ecx,%edx) 

    # start reading the next 16 bytes
	addl	$16, %esi	
	movdqu	(%esi), %xmm0

	addl	$2, %ecx
	cmpl	%ecx, 12(%ebp)
	ja	1b

    addl    $160, %esp
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

	.globl	swap_sign_mag
swap_sign_mag:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

    # let's check if we should actually do something at all
    movl    12(%ebp), %eax
    cmpl    $16, %eax
    jb 2f

    # free regs: eax, ebx, ecx, edx, esi, edi
	movl	8(%ebp), %esi     # src 
	xorl	%ecx, %ecx        # cnt
    
    # free regs: eax, ebx, edx, edi

    # initiate first dataread into xmm0
	movdqu	(%ecx, %esi), %xmm0

    # precompute the masks we use in
    # the extraction
    # edi = baseaddress for the 16-byte aligned masks
    subl    $160, %esp
    movl    %esp, %edi
    addl    $15, %edi
    andl    $0xfffffff0, %edi   # 16-byte aligned mask base

    # free regs: eax, ebx, edx, 
    # we have only one destination, load it into edx
    movl    16(%ebp), %edx

    # free regs: eax, ebx
    # move the number of bytes to process into ebx
    movl    12(%ebp), %ebx
    # free regs: eax

    # the mask for all the signs 
    movl    $0x55555555, %eax
    movl    %eax, 0(%edi)
    movl    %eax, 4(%edi)
    movl    %eax, 8(%edi)
    movl    %eax, 12(%edi)

    # id. for all the mags
    movl    $0xaaaaaaaa, %eax
    movl    %eax, 16(%edi)
    movl    %eax, 20(%edi)
    movl    %eax, 24(%edi)
    movl    %eax, 28(%edi)

1:
    # In Mk5B format ALL the signs & mags
    # are in the "wrong" place. They are
    # adjacent but in the wrong 'endianness'
    # (for signed two-bit data), as per VDIF
    # definition
   
    # load the bitmasks for the signs + mags
    movdqa  0(%edi),  %xmm7  # signs
    movdqa  16(%edi), %xmm6  # mags

    # isolate the signs + mags
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6

    # two shifts to put each bit where they should be
    psllq   $1, %xmm7
    psrlq   $1, %xmm6

    # or'ing them back together gives us 16 bytes
    # of VDIF-correct sign/mag ordering in xmm7
    por     %xmm6, %xmm7

    # write out to destination
    movdqu  %xmm7, (%ecx, %edx)

    # start loading the next 16 bytes of data
	addl	$16, %ecx
	movdqu	(%ecx, %esi), %xmm0

    # are we done yet?
	cmpl	%ecx, %ebx
	ja	1b
    # clean up stack
    addl    $160, %esp
2:
	popl	%ebx
	popl	%edi
	popl	%esi
	popl	%ebp
	ret

#movl	16(%ebp), %eax
#movdqu  %xmm1, (%eax)
#movl	24(%ebp), %eax
#movdqu  %xmm2, (%eax)
#movl	32(%ebp), %eax
#movdqu  %xmm3, (%eax)
#movl	40(%ebp), %eax
#movdqu  %xmm4, (%eax)
#jmp exit_8ch2bit_hv
	.globl	extract_8Ch2bit_hv
extract_8Ch2bit_hv:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

    # free regs: eax, ebx, ecx, edx, esi, edi
	movl	8(%ebp), %esi     # src 
	xorl	%ecx, %ecx        # cnt
    
    # divide length of block by eight -
    # we split in 8 pieces!
    #  12(%ebp) == len 
    movl    12(%ebp), %ebx
    shrl    $3, %ebx
    movl    %ebx, 12(%ebp)

    # free regs: eax, ebx, edx, edi

    # initiate first dataread into xmm0
	movdqu	(%esi), %xmm0

    # precompute the masks we use in
    # the extraction
    # edi = baseaddress for the 16-byte aligned masks
    subl    $160, %esp
    movl    %esp, %edi
    addl    $15, %edi
    andl    $0xfffffff0, %edi   # 16-byte aligned mask base

    # free regs: eax, ebx, edx, 

    # the mask for all the signs 
    movl    $0x55555555, %eax
    movl    %eax, 0(%edi)
    movl    %eax, 4(%edi)
    movl    %eax, 8(%edi)
    movl    %eax, 12(%edi)

    # id. for all the mags
    movl    $0xaaaaaaaa, %eax
    movl    %eax, 16(%edi)
    movl    %eax, 20(%edi)
    movl    %eax, 24(%edi)
    movl    %eax, 28(%edi)

    # there's 4 channels in each byte so we
    # (repeating every 16 bits)
    # create 4 masks, one for each channel

    # ChA
    movl    $0x03030303, %eax
    movl    %eax, 32(%edi)
    movl    %eax, 36(%edi)
    movl    %eax, 40(%edi)
    movl    %eax, 44(%edi)

    # ChB
    movl    $0x0c0c0c0c, %eax
    movl    %eax, 48(%edi)
    movl    %eax, 52(%edi)
    movl    %eax, 56(%edi)
    movl    %eax, 60(%edi)

    # ChC
    movl    $0x30303030, %eax
    movl    %eax, 64(%edi)
    movl    %eax, 68(%edi)
    movl    %eax, 72(%edi)
    movl    %eax, 76(%edi)

    # ChD
    movl    $0xc0c0c0c0, %eax
    movl    %eax, 80(%edi)
    movl    %eax, 84(%edi)
    movl    %eax, 88(%edi)
    movl    %eax, 92(%edi)

1:
    # In Mk5B format ALL the signs & mags
    # are in the "wrong" place. They are
    # adjacent but in the wrong 'endianness'
    # (for signed two-bit data), as per VDIF
    # definition
   
    # load the bitmasks for the signs + mags
    movdqa  0(%edi),  %xmm7  # signs
    movdqa  16(%edi), %xmm6  # mags

    # begin loading the channel masks
    movdqa  32(%edi), %xmm1
    movdqa  48(%edi), %xmm2

    # isolate the signs + mags
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6

    # finish loading the channel masks
    movdqa  64(%edi), %xmm3
    movdqa  80(%edi), %xmm4

    # two shifts to put each bit where they should be
    psllq   $1, %xmm7
    psrlq   $1, %xmm6

    # or'ing them back together gives us 4 samples 
    # in each byte
    por     %xmm6, %xmm7

    # isolate each channel
    pand    %xmm7, %xmm1    # Ch A   samples in bits 0,1
    pand    %xmm7, %xmm2    # Ch B   samples in bits 2,3
    pand    %xmm7, %xmm3    # Ch C   samples in bits 4,5
    pand    %xmm7, %xmm4    # Ch D   samples in bits 6,7

    # move all the two-bit samples to the beginning of a byte
    psrlq   $2, %xmm2
    psrlq   $4, %xmm3
    psrlq   $6, %xmm4

    # good. xmm1 holds ALL the 2-bit samples of each
    # first-channel-in-byte, of alternating channels A,E;
    # xmm2 those of B,F; xmm3 has C,G; xmm4 D,H

    # copy and shift by 14 bits to the right so they become adjacent
    # to the sample in bits 0,1
    movdqa  %xmm1, %xmm0
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1

    # id. for the other three xmm's
    movdqa  %xmm2, %xmm5
    psrlq   $14, %xmm5
    por     %xmm5, %xmm2
    psrlq   $14, %xmm5
    por     %xmm5, %xmm2
    psrlq   $14, %xmm5
    por     %xmm5, %xmm2

    movdqa  %xmm3, %xmm6
    psrlq   $14, %xmm6
    por     %xmm6, %xmm3
    psrlq   $14, %xmm6
    por     %xmm6, %xmm3
    psrlq   $14, %xmm6
    por     %xmm6, %xmm3

    movdqa  %xmm4, %xmm7
    psrlq   $14, %xmm7
    por     %xmm7, %xmm4
    psrlq   $14, %xmm7
    por     %xmm7, %xmm4
    psrlq   $14, %xmm7
    por     %xmm7, %xmm4

    # All the xmm[1-3] have in their first word (2 bytes)
    # a byte for Ch X, Ch X+4, and another in word 5 (1-based word counting)


    # create copies of xmm[1-4] such that the first double word of the high
    # double quad word comes at position 0 [so the org. and the copy have
    # the databytes in word 0
    pshufd  $2, %xmm1, %xmm5
    pshufd  $2, %xmm2, %xmm6
    pshufd  $2, %xmm3, %xmm7
    pshufd  $2, %xmm4, %xmm0

    # now we can interleave the low order bytes from 
    # xmm1+xmm5, xmm2+xmm6, xmm3+xmm7, xmm4+xmm0
    punpcklbw   %xmm5, %xmm1
    punpcklbw   %xmm6, %xmm2
    punpcklbw   %xmm7, %xmm3
    punpcklbw   %xmm0, %xmm4

    # now we have in xmm[1-4]:
    # word 0 = ch X, word 1 = ch X+4
	movl	16(%ebp), %ebx #0
	pextrw	$0, %xmm1, %eax
	movl	32(%ebp), %edx #4
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm1, %eax
    movw    %ax, (%ecx,%edx) 

	movl	20(%ebp), %ebx #1
	pextrw	$0, %xmm2, %eax
	movl	36(%ebp), %edx #5
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm2, %eax
    movw    %ax, (%ecx,%edx) 

	movl	24(%ebp), %ebx #2
	pextrw	$0, %xmm3, %eax
	movl	40(%ebp), %edx #7
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm3, %eax
    movw    %ax, (%ecx,%edx)

	movl	28(%ebp), %ebx #3
	pextrw	$0, %xmm4, %eax
	movl	44(%ebp), %edx #8
    movw    %ax, (%ecx,%ebx) 
	pextrw	$1, %xmm4, %eax
    movw    %ax, (%ecx,%edx)

    # start loading the next 16 bytes of data
	addl	$16, %esi	
	movdqu	(%esi), %xmm0

#if 0
    # start writing out the data
    # function arguments
    #  8(%ebp)  == src 
    #  12(%ebp) == len 
    #  16(%ebp) == d0 
    #  20(%ebp) == d1
    #  24(%ebp) == d2
    #  28(%ebp) == d3
    #    etc
    # free regs: eax, ebx, edx, 

    # xmm1 has Ch0,4
	pextrw	$0, %xmm1, %eax
	pextrw	$4, %xmm1, %ebx
    xchgb   %ah, %bl

	movl	16(%ebp), %edx
	movw	%ax, (%ecx, %edx)
	movl	32(%ebp), %edx
	movw	%bx, (%ecx, %edx)

    # xmm2 has Ch1,5
	pextrw	$0, %xmm2, %eax
	pextrw	$4, %xmm2, %ebx
    xchgb   %ah, %bl

	movl	20(%ebp), %edx
	movw	%ax, (%ecx, %edx)
	movl	36(%ebp), %edx
	movw	%bx, (%ecx, %edx)

    # xmm3 has Ch2,6
	pextrw	$0, %xmm2, %eax
	pextrw	$4, %xmm2, %ebx
    xchgb   %ah, %bl

	movl	24(%ebp), %edx
	movw	%ax, (%ecx, %edx)
	movl	40(%ebp), %edx
	movw	%bx, (%ecx, %edx)

    # xmm4 has Ch3,7
	pextrw	$0, %xmm2, %eax
	pextrw	$4, %xmm2, %ebx
    xchgb   %ah, %bl

	movl	28(%ebp), %edx
	movw	%ax, (%ecx, %edx)
	movl	44(%ebp), %edx
	movw	%bx, (%ecx, %edx)
#endif
	addl	$2, %ecx
	cmpl	%ecx, 12(%ebp)
	ja	1b

    # clean up stack
    addl    $160, %esp
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

#movl	16(%ebp), %eax
#psrld   $4, %xmm5
#pslld   $4, %xmm4
#movdqu  %xmm5, (%eax)
#movdqu  %xmm4, 16(%eax)
    # shift the even bits left by 28, the odd left by 36
#psrlq   $28, %xmm5
#    psllq   $4, %xmm5
#    psrlq   $36, %xmm4

#movdqu  %xmm5, 32(%eax)
#movdqu  %xmm4, 48(%eax)
#jmp exit_16ch_hv

	.globl	extract_16Ch2bit1to2_hv
extract_16Ch2bit1to2_hv:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	8(%ebp), %esi    # src
	xorl	%ecx, %ecx       # cnt

    # immediately issue the first read
	movdqu	(%esi), %xmm0

    # free: eax, ebx, edx, edi

    # ChA = bits 0 2 4 6
    # ChB = bits 1 3 5 7
    # ChC = bits 8 10 12 14
    # etc
    # precompute the eight 16-byte bitmasks
    # for the individual bits
    # edi = baseaddress for the 16-byte aligned masks
    subl    $160, %esp
    movl    %esp, %edi
    addl    $15, %edi
    andl    $0xfffffff0, %edi   # 16-byte aligned mask base

    # we have four shifts to do: -4, -1, +1, +4
    #   (negative == right == toward bit 0)
    # one for each of sign and mag and one for each
    # of ChA and ChB [there's two channels in each byte]

    #  ChA mag (shift -4)
    movl    $0x50505050, %eax
    movl    %eax, 0(%edi)
    movl    %eax, 4(%edi)
    movl    %eax, 8(%edi)
    movl    %eax, 12(%edi)

    # ChA sign (shift +1)
    movl    $0x05050505, %eax
    movl    %eax, 16(%edi)
    movl    %eax, 20(%edi)
    movl    %eax, 24(%edi)
    movl    %eax, 28(%edi)

    # ChB mag (shift -1)
    movl    $0xa0a0a0a0, %eax
    movl    %eax, 32(%edi)
    movl    %eax, 36(%edi)
    movl    %eax, 40(%edi)
    movl    %eax, 44(%edi)

    # ChB sign (shift +4)
    movl    $0x0a0a0a0a, %eax
    movl    %eax, 48(%edi)
    movl    %eax, 52(%edi)
    movl    %eax, 56(%edi)
    movl    %eax, 60(%edi)

    # free registers eax, ebx, edx

	.align	16
1:
    # Re-order the 4 32bit datawords of alternating headstacks
    # 0, 1, 0, 1 => 0, 0, 1, 1
    pshufd  $216, %xmm0, %xmm0

    # bitmask ChA mag -> xmm7; ChA sgn -> xmm6; ChB mag -> xmm5; ChB sgn -> xmm4
    movdqa  0(%edi),   %xmm7
    movdqa  16(%edi),  %xmm6
    movdqa  32(%edi),  %xmm5
    movdqa  48(%edi),  %xmm4

    # isolate the bits
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6
    pand    %xmm0, %xmm5
    pand    %xmm0, %xmm4

    # shift bits to where they should go
    psrld   $4, %xmm7
    pslld   $1, %xmm6
    psrld   $1, %xmm5
    pslld   $4, %xmm4

    # Now we can merge the signs and magnitudes of 
    # each channel, ChA into xmm7, ChB into xmm5
    por     %xmm6, %xmm7
    por     %xmm4, %xmm5

    # Note: I use 1-based dword numbering below!
    # The nibbles in the 2nd [and 4th] dword
    # are now in the same location as those in
    # 1st and 3rd. We must take the nibbles from 
    # dword 1 and 3 and merge them (id. for 2,4)
    # such that they form a byte. Half of the nibbles
    # are already at the correct position (dword 1, dword 4).
    # Create copies of the two xmm regs and shift the
    # nibbles to where to need to go:
    movdqa  %xmm7, %xmm6
    movdqa  %xmm5, %xmm4

    psllq   $4, %xmm6  # xmm6 now has ChA hi nibbles
    psrlq   $4, %xmm4  # xmm4 now has ChB lo nibbles 
   
    # now all that's left is to move the 2nd word to the first
    # position (and 4 -> 3). just shift the double quad
    # words by 4 bytes and we should be good to go
    psrldq  $4, %xmm6
    psrldq  $4, %xmm4

    # finally merge those together
    por     %xmm6, %xmm7
    por     %xmm4, %xmm5

    # done!
    # xmm7:
    #     2words in word 0 of qw0 (ch 0, 2, 4, 6)    [word 0]
    #     2words in word 0 of qw1 (ch 8, 10, 12, 14) [word 4]
    # xmm5
    #     2words in word 0 of qw0 (ch 1, 3, 5, 7)    [word 0]
    #     2words in word 0 of qw1 (ch 9, 11, 13, 15) [word 4]
    #  == 8 words total == 16 bytes

    # we can start reading the next 16 bytes into xmm0
	addl	$16, %esi	
	movdqu	(%esi), %xmm0

    # Todo: figure out the order of the channels
    #       and fix the addresses
    # Load function arguments
    #  8(%ebp)  == src 
    #  12(%ebp) == len 
    #  16(%ebp) == d0 
    #  20(%ebp) == d1
    #  24(%ebp) == d2
    #  28(%ebp) == d3
    #    etc
    # extract bytes from xmm7
	pextrw	$0, %xmm7, %eax
	movl	16(%ebp), %ebx
	movl	24(%ebp), %edx
	movb	%al, (%ecx, %ebx)
    movb	%ah, (%ecx, %edx)

	pextrw	$1, %xmm7, %eax
	movl	32(%ebp), %ebx
	movl	40(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	pextrw	$4, %xmm7, %eax
	movl	48(%ebp), %ebx
	movl	56(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	pextrw	$5, %xmm7, %eax
	movl	64(%ebp), %ebx
	movl	72(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

    # processing xmm5
	pextrw	$0, %xmm5, %eax
	movl	20(%ebp), %ebx
	movl	28(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	pextrw	$1, %xmm5, %eax
	movl	36(%ebp), %ebx
	movl	44(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	pextrw	$4, %xmm5, %eax
	movl	52(%ebp), %ebx
	movl	60(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	pextrw	$5, %xmm5, %eax
	movl	68(%ebp), %ebx
	movl	76(%ebp), %edx
	movb	%al, (%ecx, %ebx)
	movb	%ah, (%ecx, %edx)

	addl	$1, %ecx
	cmpl	%ecx, 12(%ebp)
	ja	1b

    addl    $160, %esp
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
    # Re-order the 4 32bit words of alternating headstacks
    # 0, 1, 0, 1 => 0, 0, 1, 1
    pshufd  $216, %xmm0, %xmm0
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
.globl split16bitby4
split16bitby4:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%edi
	pushl	%esi
    pushl   %ebx

    # issue the first read
	movl	8(%ebp),  %esi  # src 
    movdqu  (%esi), %xmm0

    # divide size by 8 [size is in bytes,
    # we do words]
    movl    12(%ebp), %ecx
    shrl    $2, %ecx
    movl    %ecx, 12(%ebp)
    xorl    %ecx, %ecx

	movl	16(%ebp), %eax  # dst0
	movl	20(%ebp), %ebx  # dst1
	movl	24(%ebp), %edx  # dst2
	movl	28(%ebp), %edi  # dst3

    cmpl    %ecx, 12(%ebp)
    jbe L23
L62:
    pshuflw $216, %xmm0, %xmm1
    pshufhw $216, %xmm1, %xmm0
    pshufd  $216, %xmm0, %xmm1
    movd    %xmm1, (%ecx, %eax)
    psrldq  $4, %xmm1
    movd    %xmm1, (%ecx, %ebx)
    psrldq  $4, %xmm1
    movd    %xmm1, (%ecx, %edx)
    psrldq  $4, %xmm1
    movd    %xmm1, (%ecx, %edi)

    addl    $4, %ecx
    addl    $16, %esi
	cmpl	%ecx, 12(%ebp)
    movdqu  (%esi), %xmm0
	ja	L62
L23:
    popl    %ebx
	popl	%esi
	popl	%edi
	leave
	ret

.globl split32bitby2
split32bitby2:
	pushl	%ebp
	movl	%esp, %ebp

	pushl	%esi
    pushl   %ebx

	movl	8(%ebp),  %esi  # src
	movl	12(%ebp), %ebx  # len
	movl	16(%ebp), %eax  # dst0
	movl	20(%ebp), %ebx  # dst1

    # issue the first read
    movdqu  (%esi), %xmm0

    # divide size by 2 [we split in 2]
    # and zero the counter
    shrl    $1, %ebx
    xorl    %ecx, %ecx

    cmpl    %ecx, %ebx
    jbe L24
L63:
    pshufd  $216, %xmm0, %xmm1
    movq    %xmm1, (%ecx, %eax)
    psrldq  $8, %xmm1
    movq    %xmm1, (%ecx, %ebx)

    addl    $8, %ecx
    addl    $16, %esi
    cmpl    %ecx, %ebx
    movdqu  (%esi), %xmm0
	ja	L63
L24:
    popl    %ebx
	popl	%esi
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
