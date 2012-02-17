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

.globl split8bitby4
split8bitby4:
	pushq	%rbp
	movq	%rsp, %rbp

#if 0
    /* save function arguments on stack */
	movq	%rdi, -8(%rbp)  /* src */
	movq	%rsi, -16(%rbp) /* len */
	movq	%rdx, -24(%rbp) /* dst0 */
	movq	%rcx, -32(%rbp) /* dst1 */
	movq	%r8, -40(%rbp)  /* dst2 */
	movq	%r9, -48(%rbp)  /* dst3 */
#endif

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
