/* https://www.airs.com/blog/archives/518 */
#ifdef __ELF__ /* Thx @MarkK! */
.section .note.GNU-stack,"",@progbits
#endif
.text

.globl extract_16Ch2bit1to2
extract_16Ch2bit1to2:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	movq	%r8, -40(%rbp)
	movq	%r9, -48(%rbp)
	leave
	ret
.globl extract_16Ch2bit1to2_hv
extract_16Ch2bit1to2_hv:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	movq	%r8, -40(%rbp)
	movq	%r9, -48(%rbp)
	leave
	ret
.globl extract_8Ch2bit1to2
extract_8Ch2bit1to2:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	movq	%r8, -40(%rbp)
	movq	%r9, -48(%rbp)
	leave
	ret

.globl extract_8Ch2bit1to2_hv
extract_8Ch2bit1to2_hv:
	pushq	%rbp
	movq	%rsp, %rbp

    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14

    /* Everything will fit in the registers, leading*/
    /* to the following register utilization*/
#if 0
	movq	%rdi, -8(%rbp)    # src
	movq	%rsi, -16(%rbp)   # len  [will be made len/8]
	movq	%rdx, -24(%rbp)   # d0
	movq	%rcx, -32(%rbp)   # d1
	movq	%r8, -40(%rbp)    # d2
	movq	%r9, -48(%rbp)    # d3
#endif

    /* move from call stack to regs*/
    movq    16(%rbp), %r11    # d4
    movq    24(%rbp), %r12    # d5
    movq    32(%rbp), %r13    # d6
    movq    40(%rbp), %r14    # d7

#if 0
    %rax                      # tmp 1
    %rbx                      # tmp 2
    %r10                      # cnt
#endif

    /* divide len by 8, zero out count*/
    shrq    $3, %rsi
	xorq	%r10, %r10        # cnt

    /* immediately issue the first read*/
	movdqu	(%rdi), %xmm0

    /* Ch0 = bits 0 2 4 6*/
    /* Ch1 = bits 1 3 5 7*/
    /* Ch2 = bits 8 10 12 14*/
    /* etc*/
    /* make room on the stack for the masks*/
    /* stack is already 16-byte aligned!*/
    subq    $160, %rsp

    /* each byte has four bits (two samples) of a channel*/
    /* we can easily rearrange them so each channel ends*/
    /* up in a nibble. Later rearranging the nibbles */
    /* will quickly give us a byte of each channel*/

    /* ch a, mag [and c, e, g]*/
    movq    $0x5050505050505050, %rax
    movq    %rax, 0(%rsp)
    movq    %rax, 8(%rsp)

    /* ch a, sign [....]*/
    movq    $0x0505050505050505, %rax
    movq    %rax, 16(%rsp)
    movq    %rax, 24(%rsp)

    /* ch b, mag [and d,f,h]*/
    movq    $0xa0a0a0a0a0a0a0a0, %rax
    movq    %rax, 32(%rsp)
    movq    %rax, 40(%rsp)

    /* ch b, sign [...]*/
    movq    $0x0a0a0a0a0a0a0a0a, %rax
    movq    %rax, 48(%rsp)
    movq    %rax, 56(%rsp)

	.align	16
1:
    /* bitmasks */
    movdqa  0(%rsp),   %xmm7   # ch a, mag
    movdqa  16(%rsp),  %xmm6   # ch a, sgn
    movdqa  32(%rsp),  %xmm5   # ch b, mag
    movdqa  48(%rsp),  %xmm4   # ch b, sgn

    /* isolate the bits*/
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6
    pand    %xmm0, %xmm5
    pand    %xmm0, %xmm4

    /* shift bits to where they should go*/
    psrld   $4, %xmm7
    pslld   $1, %xmm6 
    psrld   $1, %xmm5
    pslld   $4, %xmm4

    /* we need to merge the two half nibbles*/
    por     %xmm6, %xmm7
    por     %xmm4, %xmm5

    /* xmm7 has lo nibbles filled with ch a,c,e,g*/
    /* xmm5 has hi nibbles filled with ch b,d,f,h*/

    /* create copies so we can shift + merge two nibbles to one byte*/
    movdqa  %xmm7, %xmm6
    movdqa  %xmm5, %xmm4

    /* for ch a: the next nibble is found 32 bits ahead. move to front*/
    /* then move the bits 4 bits, to the hi nibble, ie 28 bits right in total*/
    psrlq   $28, %xmm6

    /* ch b: the nibble in the second 32 bits is already in the correct*/
    /* location (the hi nibble). move the data 28 bits to the left and*/
    /* the first nibble should line up just fine*/
    psllq   $28, %xmm5

    /* merge*/
    por %xmm6, %xmm7    # bytes in xmm7, dword 1, 3 [1-based counting]
    por %xmm5, %xmm4    # bytes in xmm4, dword 2, 4 [..]

    /* start writing out the data*/

    /* process xmm7*/
    /* words (note: not *dword*) 0,4 contain data for ch a, c,*/
    /* word0:  byte1,byte0 = C0, A0*/
    /* word4:  byte1,byte0 = C1, A1  */
    /*   => xchg byte1, word0 with byte0, word1 and we are set*/
	pextrw	$0, %xmm7, %rax
	pextrw	$4, %xmm7, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%rdx)  # ch 0
    movw    %bx, (%r10,%r8)   # ch 2

    /* words 1,5 contain data for ch e, g*/
	pextrw	$1, %xmm7, %rax
	pextrw	$5, %xmm7, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%r11)  #ch 4
    movw    %bx, (%r10,%r13)  #ch 6

    /* process xmm4, words 2,6 in very much the same way*/
	pextrw	$2, %xmm4, %rax
	pextrw	$6, %xmm4, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%rcx)  # ch 1
    movw    %bx, (%r10,%r9)   # ch 3

	pextrw	$3, %xmm4, %rax
	pextrw	$7, %xmm4, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%r12)  # ch 5
    movw    %bx, (%r10,%r14)  # ch 7

    /* start reading the next 16 bytes*/
	addq	$16, %rdi
	movdqu	(%rdi), %xmm0

	addq	$2, %r10
	cmpq	%r10, %rsi
	ja	1b

    addq    $160, %rsp

    popq   %r14
    popq   %r13
    popq   %r12
    popq   %r11
    popq   %r10
    popq   %rcx
    popq   %rbx
    popq   %rax
	leave
	ret


.globl extract_4Ch2bit1to2
extract_4Ch2bit1to2:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	movq	%r8, -40(%rbp)
	movq	%r9, -48(%rbp)
	leave
	ret
.globl extract_2Ch2bit1to2
extract_2Ch2bit1to2:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	leave
	ret
.globl extract_8Ch2bit
extract_8Ch2bit:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	%rcx, -32(%rbp)
	movq	%r8, -40(%rbp)
	movq	%r9, -48(%rbp)
	leave
	ret

	.globl	swap_sign_mag
swap_sign_mag:
	pushq	%rbp
	movq	%rsp, %rbp

    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14

    /* Everything will fit in the registers, leading*/
    /* to the following register utilization*/
#if 0
	movq	%rdi, -8(%rbp)    # src
	movq	%rsi, -16(%rbp)   # len 
	movq	%rdx, -24(%rbp)   # d0
#endif

    /* do we need to do anything at all?*/
    cmpq    $16, %rsi
    jb 2f
	xorq	%r10, %r10        # cnt

    /* immediately issue the first read*/
	movdqu	(%r10, %rdi), %xmm0

    /* make room on the stack for the masks*/
    /* stack is already 16-byte aligned!*/
    subq    $160, %rsp

    /* each byte has four bits (two samples) of a channel*/
    /* we can easily rearrange them so each channel ends*/
    /* up in a nibble. Later rearranging the nibbles */
    /* will quickly give us a byte of each channel*/

    /* the mask for all the signbits*/
    movq    $0x5555555555555555, %rax
    movq    %rax, 0(%rsp)
    movq    %rax, 8(%rsp)

    /* all the magnitude bits*/
    movq    $0xaaaaaaaaaaaaaaaa, %rax
    movq    %rax, 16(%rsp)
    movq    %rax, 24(%rsp)

	.align	16
1:
    /* In Mk5B format ALL the signs & mags*/
    /* are in the "wrong" place. They are*/
    /* adjacent but in the wrong  endianness */
    /* (for signed two-bit data), as per VDIF*/
    /* definition*/
   
    /* load the bitmasks for the signs + mags*/
    movdqa  0(%rsp),  %xmm7  # signs
    movdqa  16(%rsp), %xmm6  # mags

    /* isolate the signs + mags*/
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6

    /* two shifts to put each bit where they should be*/
    psllq   $1, %xmm7
    psrlq   $1, %xmm6

    /* or-ing them back together gives us 4 samples */
    /* in each byte*/
    por     %xmm6, %xmm7

    /* write out xmm7*/
    movdqu  %xmm7, (%r10, %rdx)

    /* start reading next 16 bytes*/
	addq	$16, %r10
    movdqu  (%r10, %rdi), %xmm0
    /* are we done yet?*/
	cmpq	%r10, %rsi
	ja	1b
    addq    $160, %rsp

2:
    popq   %r14
    popq   %r13
    popq   %r12
    popq   %r11
    popq   %r10
    popq   %rcx
    popq   %rbx
    popq   %rax
    leave
    ret

	.globl	extract_8Ch2bit_hv
extract_8Ch2bit_hv:
	pushq	%rbp
	movq	%rsp, %rbp

    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14

    /* Everything will fit in the registers, leading*/
    /* to the following register utilization*/
#if 0
	movq	%rdi, -8(%rbp)    # src
	movq	%rsi, -16(%rbp)   # len  [will be made len/8]
	movq	%rdx, -24(%rbp)   # d0
	movq	%rcx, -32(%rbp)   # d1
	movq	%r8, -40(%rbp)    # d2
	movq	%r9, -48(%rbp)    # d3
#endif

    /* move from call stack to regs*/
    movq    16(%rbp), %r11    # d4
    movq    24(%rbp), %r12    # d5
    movq    32(%rbp), %r13    # d6
    movq    40(%rbp), %r14    # d7

#if 0
    %rax                      # tmp 1
    %rbx                      # tmp 2
    %r10                      # cnt
#endif

    /* divide len by 8, zero out count*/
    shrq    $3, %rsi
	xorq	%r10, %r10        # cnt

    /* immediately issue the first read*/
	movdqu	(%rdi), %xmm0

    /* make room on the stack for the masks*/
    /* stack is already 16-byte aligned!*/
    subq    $160, %rsp

    /* each byte has four bits (two samples) of a channel*/
    /* we can easily rearrange them so each channel ends*/
    /* up in a nibble. Later rearranging the nibbles */
    /* will quickly give us a byte of each channel*/

    /* the mask for all the signbits*/
    movq    $0x5555555555555555, %rax
    movq    %rax, 0(%rsp)
    movq    %rax, 8(%rsp)

    /* all the magnitude bits*/
    movq    $0xaaaaaaaaaaaaaaaa, %rax
    movq    %rax, 16(%rsp)
    movq    %rax, 24(%rsp)

    /* there is 4 channels in each byte so we*/
    /* (repeating every 16 bits)*/
    /* create 4 masks, one for each channel*/

    /* ch a*/
    movq    $0x0303030303030303, %rax
    movq    %rax, 32(%rsp)
    movq    %rax, 40(%rsp)

    /* ch b*/
    movq    $0x0c0c0c0c0c0c0c0c, %rax
    movq    %rax, 48(%rsp)
    movq    %rax, 56(%rsp)

    /* ch c*/
    movq    $0x3030303030303030, %rax
    movq    %rax, 64(%rsp)
    movq    %rax, 72(%rsp)

    /* ChD*/
    movq    $0xc0c0c0c0c0c0c0c0, %rax
    movq    %rax, 80(%rsp)
    movq    %rax, 88(%rsp)

	.align	16
1:
    /* In Mk5B format ALL the signs & mags*/
    /* are in the "wrong" place. They are*/
    /* adjacent but in the wrong endianness*/
    /* (for signed two-bit data), as per VDIF*/
    /* definition*/
   
    /* load the bitmasks for the signs + mags*/
    movdqa  0(%rsp),  %xmm7  # signs
    movdqa  16(%rsp), %xmm6  # mags

    /* begin loading the channel masks*/
    movdqa  32(%rsp), %xmm1
    movdqa  48(%rsp), %xmm2

    /* isolate the signs + mags*/
    pand    %xmm0, %xmm7
    pand    %xmm0, %xmm6

    /* finish loading the channel masks*/
    movdqa  64(%rsp), %xmm3
    movdqa  80(%rsp), %xmm4

    /* two shifts to put each bit where they should be*/
    psllq   $1, %xmm7
    psrlq   $1, %xmm6

    /* or-ing them back together gives us 4 samples */
    /* in each byte*/
    por     %xmm6, %xmm7

    /* isolate each channel*/
    pand    %xmm7, %xmm1    # Ch A   samples in bits 0,1 [E in 8,9]
    pand    %xmm7, %xmm2    # Ch B   samples in bits 2,3 [F in 10,11]
    pand    %xmm7, %xmm3    # Ch C   samples in bits 4,5 [G in 12,13]
    pand    %xmm7, %xmm4    # Ch D   samples in bits 6,7 [H in 14,15]

    /* move all the two-bit samples to the beginning of a byte*/
    psrlq   $2, %xmm2
    psrlq   $4, %xmm3
    psrlq   $6, %xmm4

    /* good. xmm1 holds ALL the 2-bit samples of each*/
    /* first-channel-in-byte, of alternating channels A,E;*/
    /* xmm2 those of B,F; xmm3 has C,G; xmm4 D,H*/

    /* copy and shift by 14 bits to the right so they become adjacent*/
    /* to the sample in bits 0,1*/
    movdqa  %xmm1, %xmm0
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1
    psrlq   $14, %xmm0
    por     %xmm0, %xmm1

    /* id. for the other three xmm registers*/
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

    /* All the xmm[1-3] have in their first word (2 bytes)*/
    /* a byte for Ch X, Ch X+4, and another in word 5 (1-based word counting)*/

    /* start reading the next 16 bytes*/
	addq	$16, %rdi
	movdqu	(%rdi), %xmm0

    /* start writing out the data*/
    /* xmm1 has Ch0,4*/
	pextrw	$0, %xmm1, %eax
	pextrw	$4, %xmm1, %ebx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%rdx)  # ch 0
    movw    %bx, (%r10,%r11)  # ch 4

    /* xmm2 has Ch1,5*/
	pextrw	$0, %xmm2, %eax
	pextrw	$4, %xmm2, %ebx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%rcx)  # ch 1
    movw    %bx, (%r10,%r12)  # ch 5

    /* xmm3 has Ch2,6*/
	pextrw	$0, %xmm3, %eax
	pextrw	$4, %xmm3, %ebx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%r8)   # ch 2
    movw    %bx, (%r10,%r13)  # ch 6

    /* xmm4 has Ch3,7*/
	pextrw	$0, %xmm4, %eax
	pextrw	$4, %xmm4, %ebx
    xchgb   %ah, %bl
    movw    %ax, (%r10,%r9)   # ch 3
    movw    %bx, (%r10,%r14)  # ch 7


	addq	$2, %r10
	cmpq	%r10, %rsi
	ja	1b

    addq    $160, %rsp

    popq   %r14
    popq   %r13
    popq   %r12
    popq   %r11
    popq   %r10
    popq   %rcx
    popq   %rbx
    popq   %rax
    leave
    ret

.globl split16bitby2
split16bitby2:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp) /* src */
	movq	%rsi, -16(%rbp) /* len */
	movq	%rdx, -24(%rbp) /* d0 */
	movq	%rcx, -32(%rbp) /* d1 */
    jmp     L2
L6:
    movdqu  (%rdi), %xmm0
    pshuflw $216, %xmm0, %xmm1
    pshufhw $216, %xmm1, %xmm0
    pshufd  $216, %xmm0, %xmm1
    movq    %xmm1, (%rdx)
    psrldq  $8, %xmm1
    movq    %xmm1, (%rcx)
    addq    $16, %rdi
    addq    $8,  %rdx
    addq    $8,  %rcx
	subq	$16, %rsi
L2:
	cmpq	$15, %rsi
	ja	L6
	leave
	ret

#if 0
    /* for split8bitby4 the argument <-> stack mapping is: */
	movq	%rdi, -8(%rbp)  /* src */
	movq	%rsi, -16(%rbp) /* len */
	movq	%rdx, -24(%rbp) /* dst0 */
	movq	%rcx, -32(%rbp) /* dst1 */
	movq	%r8, -40(%rbp)  /* dst2 */
	movq	%r9, -48(%rbp)  /* dst3 */
#endif

#if SSE>40
.globl split8bitby4
split8bitby4:
	pushq	%rbp
	movq	%rsp, %rbp

    /* Make room for the 16-byte shuffling mask */
    subq    $16, %rsp

    /* make room for a 16-byte shuffling mask
       on the stack. 
       Load 1 128 bit shuffle mask in 4x32bit steps masks 
       such that the bytes come out as
       0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15
       then copy them into xmm7 */
    movl    $0x0f0b0703, 12(%rsp)
    movl    $0x0e0a0602, 8(%rsp)
    movl    $0x0d090501, 4(%rsp)
    movl    $0x0c080400, 0(%rsp)
    movdqu  (%rsp), %xmm7

    addq    $16, %rsp

    /* initiate read of 16 bytes into xmm1 */
    movdqu  (%rdi), %xmm1

    /* divide length by 16 */
    shr     $4, %rsi

    jmp     L3a
L7a:
    /* copy data into xmm0 - the next read will be initiated
       somewhere below */
    movdqa  %xmm1, %xmm0

    /* xmm7 has the shuffling mask */
    pshufb  %xmm7, %xmm0

    /* and copy out the 4*4bytes */
    /* pextrd is only available in SSE4.1 */
    pextrd  $0, %xmm0, (%rdx)
    pextrd  $1, %xmm0, (%rcx)
    pextrd  $2, %xmm0, (%r8)
    pextrd  $3, %xmm0, (%r9)

    /* already start reading the next word into xmm1 */
    addq    $16, %rdi
    movdqu  (%rdi), %xmm1

    /* update loopvariables */
    decq    %rsi
    addq    $4, %rdx
    addq    $4, %rcx
    addq    $4, %r8
    addq    $4, %r9
L3a:
	cmpq	$0, %rsi
	ja	L7a
	leave
	ret

#else /* SSE4.1 */

#if 0
    /* save function arguments on stack */
	movq	%rdi, -8(%rbp)  /* src */
	movq	%rsi, -16(%rbp) /* len */
	movq	%rdx, -24(%rbp) /* dst0 */
	movq	%rcx, -32(%rbp) /* dst1 */
	movq	%r8, -40(%rbp)  /* dst2 */
	movq	%r9, -48(%rbp)  /* dst3 */
#endif
.globl split8bitby4
split8bitby4:
	pushq	%rbp
	movq	%rsp, %rbp

    /* initiate read of 16 bytes into xmm1 */
    movdqu  (%rdi), %xmm0

    jmp     L3a
L7a:
    /* copy data into xmm0 - the next read will be initiated
       somewhere below */
    movdqa  %xmm0, %xmm1

    /* go through this byte by byte _sigh_
       sse2 does not have byte-shuffling */
    /* reshuffle by 16 bits such that we have
       the even words followed by the odd words,
       then we can go through them two destinations
       at a time */
    pshuflw $216, %xmm1, %xmm2
    pshufhw $216, %xmm2, %xmm1
    pshufd  $216, %xmm1, %xmm2
    /* The order of the bytes now is:
       0 1 4 5 8 9 12 13 2 3 6 7 10 11 14 15 */

    /* Process the first 8 bytes (2 times 4 chunks) - they
       go to d0 + d1 */
    /* load the two desinations */
    movd    %xmm2, %rax
    movq    %rax, %rbx
    shrq    $16, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%rdx)
    movw    %bx, (%rcx)

    psrldq  $4, %xmm2
    movd    %xmm2, %rax
    movq    %rax, %rbx
    shrq    $16, %rbx
    xchgb   %ah, %bl
    movw    %ax, 2(%rdx)
    movw    %bx, 2(%rcx)

    /* Remaining 64 bits go to d2 + d3 */
    psrldq  $4, %xmm2
    movd    %xmm2, %rax
    movq    %rax, %rbx
    shrq    $16, %rbx
    xchgb   %ah, %bl
    movw    %ax, (%r8)
    movw    %bx, (%r9)

    psrldq  $4, %xmm2
    movd    %xmm2, %rax
    movq    %rax, %rbx
    shrq    $16, %rbx
    xchgb   %ah, %bl
    movw    %ax, 2(%r8)
    movw    %bx, 2(%r9)

    /* already start reading the next word into xmm1 */
    addq    $16, %rdi
    movdqu  (%rdi), %xmm0
    subq    $16, %rsi
    addq    $4, %rdx
    addq    $4, %rcx
    addq    $4, %r8
    addq    $4, %r9
L3a:
	cmpq	$15, %rsi
	ja	L7a
	leave
	ret

#endif /* SSE2 */

.globl split8bitby4_old
split8bitby4_old:
	pushq	%rbp
	movq	%rsp, %rbp
    /* save function arguments on stack */
	movq	%rdi, -8(%rbp)  /* src */
	movq	%rsi, -16(%rbp) /* len */
	movq	%rdx, -24(%rbp) /* dst0 */
	movq	%rcx, -32(%rbp) /* dst1 */
	movq	%r8, -40(%rbp)  /* dst2 */
	movq	%r9, -48(%rbp)  /* dst3 */

    /* Load the shuffle masks into xmm4-7 */
    movq    $0x0c080400, %rdx
    movd    %rdx, %xmm4
    movq    $0x0d090501, %rdx
    movd    %rdx, %xmm5
    movq    $0x0e0a0602, %rdx
    movd    %rdx, %xmm6
    movq    $0x0f0b0703, %rdx
    movd    %rdx, %xmm7

    /* Put back value of rdx (which we used) */
	movq	-24(%rbp), %rdx /* dst0 */

    jmp     L3
L7:
    /* 16 bytes or src data in xmm0 */
    movdqu  (%rdi), %xmm0
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
    movd    %xmm0, (%rdx)
    movd    %xmm1, (%rcx)
    movd    %xmm2, (%r8)
    movd    %xmm3, (%r9)
    /* update loopvariables */
    addq    $16, %rdi
    addq    $4,  %rdx
    addq    $4,  %rcx
    addq    $4,  %rdx
    addq    $4,  %rcx
	subq	$16, %rsi
L3:
	cmpq	$15, %rsi
	ja	L7
	leave
	ret


.globl split16bitby4
split16bitby4:
	pushq	%rbp
	movq	%rsp, %rbp

/*   function arg -> reg mapping*/
/*	movq	%rdi, -8(%rbp)   src  */
/*	movq	%rsi, -16(%rbp)  len  */
/*	movq	%rdx, -24(%rbp)  dst0 */
/*	movq	%rcx, -32(%rbp)  dst1 */
/*	movq	%r8, -40(%rbp)   dst2 */
/*	movq	%r9, -48(%rbp)   dst3 */

    /* issue the first read*/
    movdqu  (%rdi), %xmm0

    /* divide size by 4 [we split into 4 chunks]*/
    /* and zero the counter*/
    shrq    $2, %rsi
    xorq    %r10, %r10

    cmpq    %r10, %rsi
    jbe L23
L62:
    /* HV: 16sep2013 - Shuffling things around like this is completely wrong*/
    /*                 end up with mixed data*/
    /*                 Should be better now!*/
    pshufd  $216, %xmm0, %xmm1
    pshuflw $216, %xmm1, %xmm0
    pshufhw $216, %xmm0, %xmm1
    movd    %xmm1, (%r10, %rdx)
    psrldq  $4, %xmm1
    movd    %xmm1, (%r10, %rcx)
    psrldq  $4, %xmm1
    movd    %xmm1, (%r10, %r8)
    psrldq  $4, %xmm1
    movd    %xmm1, (%r10, %r9)

    addq    $4, %r10
    addq    $16, %rdi
	cmpq	%r10, %rsi
    movdqu  (%rdi), %xmm0
	ja	L62
L23:
	leave
	ret

.globl split32bitby2
split32bitby2:
	pushq	%rbp
	movq	%rsp, %rbp

/*   function arg -> reg mapping*/
/*	movq	%rdi, -8(%rbp)   src*/
/*	movq	%rsi, -16(%rbp)  len */
/*	movq	%rdx, -24(%rbp)  dst0 */
/*	movq	%rcx, -32(%rbp)  dst1 */

    /* issue the first read*/
    movdqu  (%rdi), %xmm0

    /* divide size by 2 [we split in 2]*/
    /* and zero the counter*/
    shrq    $1, %rsi
    xorq    %r10, %r10

    cmpq    %r10, %rsi
    jbe L24
L64:
    pshufd  $216, %xmm0, %xmm1
    movq    %xmm1, (%r10, %rdx)
    psrldq  $8, %xmm1
    movq    %xmm1, (%r10, %rcx)

    addq    $8, %r10
    addq    $16, %rdi
	cmpq	%r10, %rsi
    movdqu  (%rdi), %xmm0
	ja	L64
L24:
	leave
	ret
