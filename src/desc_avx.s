	.intel_syntax noprefix

	.section .text

.macro rotate_s num
	mov r12d, r11d
	rol r12d, \num
.endm

.macro rotate_d num
	vpslld xmm4, xmm3, \num
	vpsrld xmm5, xmm3, 32 - \num
	vpor xmm4, xmm4, xmm5
.endm

.macro rotate_q num
	vpslld ymm4, ymm3, \num
	vpsrld ymm5, ymm3, 32 - \num
	vpor ymm4, ymm4, ymm5
.endm


.macro pi1_s
	xor r10d, r9d
.endm

.macro pi2_s key1
	vpextrd r11d, \key1, 0
	add r11d, r10d

	rotate_s 1
	add r11d, r12d
	dec r11d

	rotate_s 4
	xor r11d, r12d

	xor r9d, r11d
.endm

.macro pi3_s key1, key2
	vpextrd r11d, \key1, 0
	add r11d, r9d

	rotate_s 2
	add r11d, r12d
	inc r11d

	rotate_s 8
	xor r11d, r12d

	vpextrd r12d, \key2, 0
	add r11d, r12d

	rotate_s 1
	sub r12d, r11d
	mov r11d, r12d

	rotate_s 16
	or r11d, r9d
	xor r11d, r12d

	xor r10d, r11d
.endm

.macro pi4_s key1
	vpextrd r11d, \key1, 0
	add r11d, r10d

	rotate_s 2
	add r11d, r12d
	inc r11d

	xor r9d, r11d
.endm


.macro pi1_d
	vpxor xmm2, xmm2, xmm1
.endm

.macro pi2_d key1
	vpaddd xmm3, xmm2, \key1

	rotate_d 1
	vpaddd xmm3, xmm3, xmm4
	vpsubd xmm3, xmm3, xmm7

	rotate_d 4
	vpxor xmm3, xmm3, xmm4

	vpxor xmm1, xmm1, xmm3
.endm

.macro pi3_d key1, key2
	vpaddd xmm3, xmm1, \key1

	rotate_d 2
	vpaddd xmm3, xmm3, xmm4
	vpaddd xmm3, xmm3, xmm7

	rotate_d 8
	vpxor xmm3, xmm3, xmm4

	vpaddd xmm3, xmm3, \key2

	rotate_d 1
	vpsubd xmm3, xmm4, xmm3

	rotate_d 16
	vpor xmm3, xmm3, xmm1
	vpxor xmm3, xmm3, xmm4

	vpxor xmm2, xmm2, xmm3
.endm

.macro pi4_d key1
	vpaddd xmm3, xmm2, \key1

	rotate_d 2
	vpaddd xmm3, xmm3, xmm4
	vpaddd xmm3, xmm3, xmm7

	vpxor xmm1, xmm1, xmm3
.endm


.macro pi1_q
	vpxor ymm2, ymm2, ymm1
.endm

.macro pi2_q key1
	vpaddd ymm3, ymm2, \key1

	rotate_q 1
	vpaddd ymm3, ymm3, ymm4
	vpsubd ymm3, ymm3, ymm7

	rotate_q 4
	vpxor ymm3, ymm3, ymm4

	vpxor ymm1, ymm1, ymm3
.endm

.macro pi3_q key1, key2
	vpaddd ymm3, ymm1, \key1

	rotate_q 2
	vpaddd ymm3, ymm3, ymm4
	vpaddd ymm3, ymm3, ymm7

	rotate_q 8
	vpxor ymm3, ymm3, ymm4

	vpaddd ymm3, ymm3, \key2

	rotate_q 1
	vpsubd ymm3, ymm4, ymm3

	rotate_q 16
	vpor ymm3, ymm3, ymm1
	vpxor ymm3, ymm3, ymm4

	vpxor ymm2, ymm2, ymm3
.endm

.macro pi4_q key1
	vpaddd ymm3, ymm2, \key1

	rotate_q 2
	vpaddd ymm3, ymm3, ymm4
	vpaddd ymm3, ymm3, ymm7

	vpxor ymm1, ymm1, ymm3
.endm


.macro oct_block
oct_start:
	cmp al, 8
	jb oct_exit

	# block-cypher decode (ymm0, ymm5) to (ymm3, ymm4)
	# pack to ymm1: L's, ymm2: R's
	vmovdqu ymm0, [rsi]
	vpshufb ymm0, ymm0, ymm6
	add rsi, 32

	vpshufd ymm1, ymm0, 0xD8
	vpermq ymm1, ymm1, 0xD8

	vmovdqu ymm5, [rsi]
	vpshufb ymm5, ymm5, ymm6
	add rsi, 32

	vpshufd ymm3, ymm5, 0xD8
	vpermq ymm3, ymm3, 0xD8

	vperm2i128 ymm2, ymm1, ymm3, 0x20
	vperm2i128 ymm1, ymm1, ymm3, 0x31

	# save ymm5. It will be broken to be used as a tmp.
	vpextrq r10, xmm5, 0
	vpextrq r11, xmm5, 1
	vpermq ymm5, ymm5, 0x4E
	vpextrq r12, xmm5, 0
	vpextrq r13, xmm5, 1

	mov bl, 4
o_round_loop:
	pi4_q ymm15
	pi3_q ymm13, ymm14
	pi2_q ymm12
	pi1_q

	pi4_q ymm11
	pi3_q ymm9, ymm10
	pi2_q ymm8
	pi1_q

	dec bl
	jnz o_round_loop

	# unpack ymm1, ymm2 to ymm3, ymm4
	vpermq ymm1, ymm1, 0xD8
	vpermq ymm2, ymm2, 0xD8
	vpunpckldq ymm3, ymm2, ymm1
	vpunpckhdq ymm4, ymm2, ymm1

	# CBC for 1st i128. xor with previous inputs
	# rotr ymm0, ymm0, 64
	vpermq ymm1, ymm0, 0x03		# ymm1: (q0|q0)|q0|q3
	vpextrq r9, xmm1, 0
	vpinsrq xmm1, xmm1, r8, 0 	# ymm1: 00|00|q0|r8
	vpermq ymm0, ymm0, 0x09		# ymm0: (q0|q0)|q2|q1
	vperm2i128 ymm0, ymm0, ymm1, 0x02 # ymm0: q2|q1|q0|r8
	vpxor ymm3, ymm3, ymm0

	vpshufb ymm3, ymm3, ymm6 
	vmovdqu [rdi], ymm3
	add rdi, 32

	# CBC for 2nd i128. xor with previous inputs
	# restore ymm5 & slldq ymm5, 0x64
	vpinsrq xmm1, xmm1, r12, 1
	vpinsrq xmm1, xmm1, r11, 0	# ymm1: 00|00|r12|r11
	vpinsrq xmm5, xmm5, r10, 1
	vpinsrq xmm5, xmm5, r9, 0	# ymm5: 00|00|r10|r9
	vperm2i128 ymm5, ymm1, ymm5, 0x02 # ymm5: r12|r11|r10|r9
	mov r8, r13
	vpxor ymm4, ymm4, ymm5

	vpshufb ymm4, ymm4, ymm6 
	vmovdqu [rdi], ymm4
	add rdi, 32

	sub al, 8
	jmp oct_start
oct_exit:
.endm


.macro quad_block
	cmp al, 4
	jb quad_exit

	# block-cypher decode ymm0 to ymm1
	vmovdqu ymm0, [rsi]
	vpshufb ymm0, ymm0, ymm6
	add rsi, 32

	# pack to xmm1: L's, xmm2: R's
	vpshufd ymm1, ymm0, 0xD8
	vpermq ymm2, ymm1, 0x08
	vpermq ymm1, ymm1, 0x0D

	mov bl, 4
q_round_loop:
	pi4_d xmm15
	pi3_d xmm13, xmm14
	pi2_d xmm12
	pi1_d

	pi4_d xmm11
	pi3_d xmm9, xmm10
	pi2_d xmm8
	pi1_d

	dec bl
	jnz q_round_loop

	# unpack xmm1, xmm2 to ymm3
	vpermq ymm1, ymm1, 0xD8
	vpermq ymm2, ymm2, 0xD8
	vpunpckldq ymm3, ymm2, ymm1

	# CBC. xor with previous inputs
	# rotl ymm1, ymm0, 64
	vpermq ymm1, ymm0, 0x03		# ymm1: (q0|q0)|q0|q3
	vpextrq r9, xmm1, 0
	vpinsrq xmm1, xmm1, r8, 0 	# ymm1: 00|00|q0|r8
	vpermq ymm0, ymm0, 0x09		# ymm0: (q0|q0)|q2|q1
	vperm2i128 ymm0, ymm0, ymm1, 0x02 # ymm0: q2|q1|q0|r8
	mov r8, r9
	vpxor ymm3, ymm3, ymm0

	vpshufb ymm3, ymm3, ymm6
	vmovdqu [rdi], ymm3
	add rdi, 32

	sub al, 4
quad_exit:
.endm


.macro double_block
	cmp al, 2
	jb double_exit

	# block-cypher decode xmm0 to xmm1
	vmovdqu xmm0, [rsi]
	vpshufb xmm0, xmm0, xmm6
	add rsi, 16

	# pack to xmm1: L's, xmm2: R's (upper qwords are not used)
	vpshufd xmm1, xmm0, 0x0D
	vpshufd xmm2, xmm0, 0x08

	mov bl, 4
d_round_loop:
	pi4_d xmm15
	pi3_d xmm13, xmm14
	pi2_d xmm12
	pi1_d

	pi4_d xmm11
	pi3_d xmm9, xmm10
	pi2_d xmm8
	pi1_d

	dec bl
	jnz d_round_loop

	# unpack
	vpunpckldq xmm1, xmm2, xmm1

	# CBC. xor with previous inputs
	# xmm3 = xmm0 << 64 | r8
	vpshufd xmm3, xmm0, 0x4E
	vpinsrq xmm3, xmm3, r8, 0
	vpxor xmm1, xmm1, xmm3
	# save the last input
	vpextrq r8, xmm0, 1

	vpshufb xmm1, xmm1, xmm6
	vmovdqu [rdi], xmm1
	add rdi, 16

	sub al, 2
double_exit:
.endm


.macro single_block
	cmp al, 1
	jb residual
	
	# read one block (un-aligned)
	movbe rdx, qword ptr [rsi]
	add rsi, 8

	# pack. r9d: L, r10d: R
	mov r9, rdx
	mov r10d, r9d
	shr r9, 32

	mov bl, 4
s_round_loop:
	pi4_s xmm15	
	pi3_s xmm13, xmm14
	pi2_s xmm12
	pi1_s

	pi4_s xmm11
	pi3_s xmm9, xmm10
	pi2_s xmm8
	pi1_s

	dec bl
	jnz s_round_loop

	shl r9, 32
	or r9, r10

	# CBC. xor with previous input / IV
	xor r9, r8
	# save the new prev. input for the following decodes.
	mov r8, rdx

	# store result (un-algined)
	movbe qword ptr [rdi], r9
	add rdi, 8

	dec al
.endm


# residual_block: common tail-part of each desc_avx_* routines

.macro res_block
	and cl, 7
	jz residual_exit

	# OFB

	xor rdx, rdx
	# save CL to AL. CL will be used as a loop counter.
	mov al, cl

r_input_loop:
	shl rdx, 8
	mov dl, byte ptr [rsi]
	inc rsi
	dec cl
	jnz r_input_loop

	mov cl, 8
	sub cl, al
	shl cl, 3
	shl rdx, cl

	# block-cypher ENCODE r8, out to r9

	# pack. r9d: L, r10d: R
	mov r9, r8
	shr r9, 32
	mov r10d, r8d

	mov bl, 4
r_round_loop:
	pi1_s
	pi2_s xmm8
	pi3_s xmm9, xmm10
	pi4_s xmm11

	pi1_s
	pi2_s xmm12
	pi3_s xmm13, xmm14
	pi4_s xmm15

	dec bl
	jnz r_round_loop

	shl r9, 32
	or r9, r10

	xor rdx, r9
	bswap rdx

r_output_loop:
	mov byte ptr [rdi], dl
	shr rdx, 8
	inc rdi
	dec al
	jnz r_output_loop

residual_exit:
.endm


# desc_avx: main part

desc_avx:
	oct_block
	quad_block
	double_block
	single_block
residual:
	res_block
	ret
.type desc_avx, @function
.size desc_avx, . - desc_avx


#
# exported function
#

	.globl descramble2

descramble2:
	push rbp
	mov rbp, rsp
	push rbx
	push r12
	push r13

	# EDI: obuf, ESI: ibuf, EDX: prm, ECX: len (<= 184)

	# constants
	# ymm7: all 1 (8x dword 1)
	mov r8, 1
	vpinsrq xmm7, xmm7, r8, 0
	vpbroadcastd ymm7, xmm7 

	# ymm6: shuffle idx
	mov r8, 0x0001020304050607
	vpinsrq xmm6, xmm6, r8, 0
	mov r8, 0x08090A0B0C0D0E0F
	vpinsrq xmm6, xmm6, r8, 1
	vpermq ymm6, ymm6, 0x44

	# ymm8..ymm15: scheduled keys (oct-repeated W_1 .. W_8)
	vpbroadcastd ymm8,  dword ptr [rdx]
	vpbroadcastd ymm9,  dword ptr [rdx + 4]
	vpbroadcastd ymm10, dword ptr [rdx + 8]
	vpbroadcastd ymm11, dword ptr [rdx + 12]
	vpbroadcastd ymm12, dword ptr [rdx + 16]
	vpbroadcastd ymm13, dword ptr [rdx + 20]
	vpbroadcastd ymm14, dword ptr [rdx + 24]
	vpbroadcastd ymm15, dword ptr [rdx + 28]

	# r8: last input / IV
	mov r8, 0xFE27199919690911

	# AL: len / 8 (blocks)
	mov al, cl
	shr al, 3

	call desc_avx

	pop r13
	pop r12
	pop rbx
	mov rsp, rbp
	pop rbp
	ret
.type descramble2, @function
.size descramble2, . - descramble2
