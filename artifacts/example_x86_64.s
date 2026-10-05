	.file	"example.c"
	.intel_syntax noprefix
	.text
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"RBL runtime error: %s\n"
	.text
	.p2align 4
	.type	rbl_error, @function
rbl_error:
.LFB9:
	.cfi_startproc
	sub	rsp, 8
	.cfi_def_cfa_offset 16
	mov	rdx, rdi
	mov	rdi, QWORD PTR stderr[rip]
	xor	eax, eax
	lea	rsi, .LC0[rip]
	call	fprintf@PLT
	mov	edi, 1
	call	exit@PLT
	.cfi_endproc
.LFE9:
	.size	rbl_error, .-rbl_error
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align 8
.LC1:
	.string	"RBL runtime error: function '%s' not found\n"
	.text
	.p2align 4
	.type	rbl_unknown_function, @function
rbl_unknown_function:
.LFB10:
	.cfi_startproc
	sub	rsp, 8
	.cfi_def_cfa_offset 16
	mov	rdx, rdi
	mov	rdi, QWORD PTR stderr[rip]
	xor	eax, eax
	lea	rsi, .LC1[rip]
	call	fprintf@PLT
	mov	edi, 1
	call	exit@PLT
	.cfi_endproc
.LFE10:
	.size	rbl_unknown_function, .-rbl_unknown_function
	.section	.rodata.str1.1
.LC2:
	.string	"out of memory"
	.text
	.p2align 4
	.type	rbl_string_literal, @function
rbl_string_literal:
.LFB14:
	.cfi_startproc
	push	r13
	.cfi_def_cfa_offset 16
	.cfi_offset 13, -16
	push	r12
	.cfi_def_cfa_offset 24
	.cfi_offset 12, -24
	push	rbp
	.cfi_def_cfa_offset 32
	.cfi_offset 6, -32
	mov	rbp, rdi
	push	rbx
	.cfi_def_cfa_offset 40
	.cfi_offset 3, -40
	sub	rsp, 8
	.cfi_def_cfa_offset 48
	call	strlen@PLT
	lea	rdi, 1[rax]
	call	malloc@PLT
	test	rax, rax
	je	.L10
	mov	r12, QWORD PTR rbl_owned_strings_len[rip]
	cmp	r12, QWORD PTR rbl_owned_strings_cap[rip]
	mov	rbx, rax
	mov	rdi, QWORD PTR rbl_owned_strings[rip]
	je	.L16
.L8:
	mov	QWORD PTR [rdi+r12*8], rbx
	lea	rax, 1[r12]
	mov	rsi, rbp
	mov	rdi, rbx
	mov	QWORD PTR rbl_owned_strings_len[rip], rax
	call	strcpy@PLT
	add	rsp, 8
	.cfi_remember_state
	.cfi_def_cfa_offset 40
	mov	ecx, 2
	mov	rdx, rbx
	mov	eax, ecx
	pop	rbx
	.cfi_def_cfa_offset 32
	pop	rbp
	.cfi_def_cfa_offset 24
	pop	r12
	.cfi_def_cfa_offset 16
	pop	r13
	.cfi_def_cfa_offset 8
	ret
	.p2align 4,,10
	.p2align 3
.L16:
	.cfi_restore_state
	test	r12, r12
	jne	.L17
	mov	esi, 512
	mov	r13d, 64
.L9:
	call	realloc@PLT
	mov	rdi, rax
	test	rax, rax
	je	.L10
	mov	QWORD PTR rbl_owned_strings[rip], rax
	mov	QWORD PTR rbl_owned_strings_cap[rip], r13
	jmp	.L8
	.p2align 4,,10
	.p2align 3
.L17:
	mov	rsi, r12
	lea	r13, [r12+r12]
	sal	rsi, 4
	jmp	.L9
.L10:
	lea	rdi, .LC2[rip]
	call	rbl_error
	.cfi_endproc
.LFE14:
	.size	rbl_string_literal, .-rbl_string_literal
	.section	.rodata.str1.1
.LC3:
	.string	"true"
.LC4:
	.string	"false"
.LC5:
	.string	"warn"
.LC6:
	.string	"[WARN]"
.LC7:
	.string	"%ld"
.LC8:
	.string	"%.17g"
.LC9:
	.string	"error"
.LC10:
	.string	"[ERROR]"
.LC11:
	.string	"log"
.LC12:
	.string	"unknown method '%s.%s'"
	.text
	.p2align 4
	.type	rbl_method_call.constprop.0.isra.0, @function
rbl_method_call.constprop.0.isra.0:
.LFB59:
	.cfi_startproc
	push	rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	mov	rbp, rdi
	push	rbx
	.cfi_def_cfa_offset 24
	.cfi_offset 3, -24
	mov	rbx, rsi
	lea	rsi, .LC5[rip]
	sub	rsp, 264
	.cfi_def_cfa_offset 288
	call	strcmp@PLT
	test	eax, eax
	jne	.L19
	mov	rcx, QWORD PTR stdout[rip]
	mov	edx, 6
	mov	esi, 1
	lea	rdi, .LC6[rip]
.L46:
	call	fwrite@PLT
	mov	rsi, QWORD PTR stdout[rip]
	mov	edi, 32
	call	fputc@PLT
	mov	eax, DWORD PTR [rbx]
	mov	rdi, QWORD PTR stdout[rip]
	mov	rdx, QWORD PTR 8[rbx]
	cmp	eax, 2
	je	.L28
	ja	.L29
	test	eax, eax
	je	.L47
	movq	xmm0, rdx
	lea	rsi, .LC8[rip]
	mov	eax, 1
	call	fprintf@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L33
	.p2align 4,,10
	.p2align 3
.L28:
	mov	rsi, rdi
	mov	rdi, rdx
	call	fputs@PLT
	mov	rdi, QWORD PTR stdout[rip]
.L33:
	add	rsp, 264
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	mov	rsi, rdi
	mov	edi, 10
	pop	rbx
	.cfi_def_cfa_offset 16
	pop	rbp
	.cfi_def_cfa_offset 8
	jmp	fputc@PLT
	.p2align 4,,10
	.p2align 3
.L19:
	.cfi_restore_state
	lea	rsi, .LC9[rip]
	mov	rdi, rbp
	call	strcmp@PLT
	test	eax, eax
	jne	.L27
	mov	rcx, QWORD PTR stdout[rip]
	mov	edx, 7
	mov	esi, 1
	lea	rdi, .LC10[rip]
	jmp	.L46
	.p2align 4,,10
	.p2align 3
.L29:
	cmp	eax, 3
	jne	.L33
	test	dl, dl
	lea	rax, .LC4[rip]
	lea	rdx, .LC3[rip]
	mov	rsi, rdi
	cmovne	rax, rdx
	mov	rdi, rax
	call	fputs@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L33
	.p2align 4,,10
	.p2align 3
.L47:
	lea	rsi, .LC7[rip]
	xor	eax, eax
	call	fprintf@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L33
.L27:
	mov	rdi, rsp
	lea	r8, .LC11[rip]
	mov	rcx, rbp
	xor	eax, eax
	lea	rdx, .LC12[rip]
	mov	esi, 256
	call	snprintf@PLT
	mov	rdi, rsp
	call	rbl_error
	.cfi_endproc
.LFE59:
	.size	rbl_method_call.constprop.0.isra.0, .-rbl_method_call.constprop.0.isra.0
	.section	.rodata.str1.8
	.align 8
.LC13:
	.string	"internal call arity mismatch for function main"
	.section	.rodata.str1.1
.LC14:
	.string	"change number"
.LC15:
	.string	"if elif else"
.LC16:
	.string	"big"
.LC17:
	.string	"just a warning"
	.text
	.p2align 4
	.type	fn_1.isra.0, @function
fn_1.isra.0:
.LFB62:
	.cfi_startproc
	push	rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	mov	rdx, rdi
	mov	ecx, 12
	xor	eax, eax
	push	rbx
	.cfi_def_cfa_offset 24
	.cfi_offset 3, -24
	sub	rsp, 200
	.cfi_def_cfa_offset 224
	lea	rdi, 96[rsp]
	rep stosq
	test	rdx, rdx
	jne	.L53
	mov	QWORD PTR 112[rsp], 10
	movdqu	xmm0, XMMWORD PTR 104[rsp]
	lea	rsi, 16[rsp]
	xor	ebx, ebx
	mov	edi, -1
	lea	rbp, 80[rsp]
	movaps	XMMWORD PTR 16[rsp], xmm0
	call	rbl_invoke_call.constprop.0
	lea	rdi, .LC14[rip]
	call	rbl_string_literal
	lea	rsi, 32[rsp]
	mov	edi, -1
	mov	QWORD PTR 32[rsp], rax
	mov	QWORD PTR 40[rsp], rdx
	call	rbl_invoke_call.constprop.0
	lea	rsi, 48[rsp]
	mov	edi, -1
	mov	QWORD PTR 112[rsp], 20
	movdqu	xmm0, XMMWORD PTR 104[rsp]
	movaps	XMMWORD PTR 48[rsp], xmm0
	call	rbl_invoke_call.constprop.0
	lea	rdi, .LC15[rip]
	call	rbl_string_literal
	lea	rsi, 64[rsp]
	mov	edi, -1
	mov	QWORD PTR 64[rsp], rax
	mov	QWORD PTR 72[rsp], rdx
	call	rbl_invoke_call.constprop.0
	lea	rdi, .LC16[rip]
	call	rbl_string_literal
	mov	rsi, rbp
	mov	edi, -1
	mov	QWORD PTR 80[rsp], rax
	mov	QWORD PTR 88[rsp], rdx
	call	rbl_invoke_call.constprop.0
	.p2align 4
	.p2align 3
.L50:
	mov	QWORD PTR 136[rsp], rbx
	mov	rsi, rbp
	add	rbx, 1
	movdqa	xmm0, XMMWORD PTR 128[rsp]
	mov	edi, -1
	movaps	XMMWORD PTR 80[rsp], xmm0
	call	rbl_invoke_call.constprop.0
	cmp	rbx, 11
	jne	.L50
	lea	rdi, .LC17[rip]
	call	rbl_string_literal
	mov	rsi, rbp
	lea	rdi, .LC5[rip]
	mov	QWORD PTR 80[rsp], rax
	mov	QWORD PTR 88[rsp], rdx
	call	rbl_method_call.constprop.0.isra.0
	mov	rsi, rbp
	mov	edi, -1
	xor	ebp, ebp
	mov	QWORD PTR 184[rsp], 5
	movdqa	xmm0, XMMWORD PTR 176[rsp]
	movaps	XMMWORD PTR 80[rsp], xmm0
	call	rbl_invoke_call.constprop.0
	xor	edi, edi
	add	rsp, 200
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	movabs	rsi, -4294967296
	mov	ecx, edi
	pop	rbx
	.cfi_def_cfa_offset 16
	mov	rdi, rcx
	mov	rcx, rbp
	and	rcx, rsi
	mov	ecx, ecx
	mov	rbp, rcx
	mov	rcx, rdi
	and	rcx, rsi
	mov	rdx, rbp
	pop	rbp
	.cfi_def_cfa_offset 8
	or	rcx, 4
	mov	eax, ecx
	ret
.L53:
	.cfi_restore_state
	lea	rdi, .LC13[rip]
	call	rbl_error
	.cfi_endproc
.LFE62:
	.size	fn_1.isra.0, .-fn_1.isra.0
	.section	.rodata.str1.8
	.align 8
.LC18:
	.string	"internal call arity mismatch for function add"
	.text
	.p2align 4
	.type	rbl_invoke_call.constprop.0, @function
rbl_invoke_call.constprop.0:
.LFB64:
	.cfi_startproc
	sub	rsp, 24
	.cfi_def_cfa_offset 32
	cmp	edi, -1
	je	.L71
	cmp	edi, 1
	jne	.L72
	mov	edi, 1
	add	rsp, 24
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	jmp	fn_1.isra.0
	.p2align 4,,10
	.p2align 3
.L71:
	.cfi_restore_state
	mov	eax, DWORD PTR [rsi]
	mov	rdi, QWORD PTR stdout[rip]
	mov	rdx, QWORD PTR 8[rsi]
	cmp	eax, 2
	je	.L60
	ja	.L61
	test	eax, eax
	je	.L73
	movq	xmm0, rdx
	lea	rsi, .LC8[rip]
	mov	eax, 1
	call	fprintf@PLT
	mov	rdi, QWORD PTR stdout[rip]
.L57:
	mov	rsi, rdi
	mov	edi, 10
	call	fputc@PLT
	xor	esi, esi
	xor	edi, edi
	add	rsp, 24
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	movabs	rdx, -4294967296
	mov	eax, esi
	mov	rsi, rax
	mov	rax, rdi
	and	rax, rdx
	mov	eax, eax
	mov	rdi, rax
	mov	rax, rsi
	and	rax, rdx
	mov	rdx, rdi
	or	rax, 4
	ret
	.p2align 4,,10
	.p2align 3
.L61:
	.cfi_restore_state
	cmp	eax, 3
	jne	.L57
	test	dl, dl
	lea	rax, .LC4[rip]
	lea	rdx, .LC3[rip]
	mov	rsi, rdi
	cmovne	rax, rdx
	mov	rdi, rax
	call	fputs@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L57
	.p2align 4,,10
	.p2align 3
.L72:
	lea	rdi, .LC18[rip]
	call	rbl_error
	.p2align 4,,10
	.p2align 3
.L73:
	lea	rsi, .LC7[rip]
	xor	eax, eax
	call	fprintf@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L57
	.p2align 4,,10
	.p2align 3
.L60:
	mov	rsi, rdi
	mov	rdi, rdx
	call	fputs@PLT
	mov	rdi, QWORD PTR stdout[rip]
	jmp	.L57
	.cfi_endproc
.LFE64:
	.size	rbl_invoke_call.constprop.0, .-rbl_invoke_call.constprop.0
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB54:
	.cfi_startproc
	push	r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	xor	edi, edi
	push	rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	push	rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	call	fn_1.isra.0
	mov	rax, QWORD PTR rbl_owned_strings_len[rip]
	mov	r12, QWORD PTR rbl_owned_strings[rip]
	lea	rbp, [r12+rax*8]
	test	rax, rax
	je	.L77
	mov	rbx, r12
	.p2align 4
	.p2align 3
.L76:
	mov	rdi, QWORD PTR [rbx]
	add	rbx, 8
	call	free@PLT
	cmp	rbp, rbx
	jne	.L76
.L77:
	mov	rdi, r12
	call	free@PLT
	pop	rbx
	.cfi_def_cfa_offset 24
	xor	eax, eax
	pop	rbp
	.cfi_def_cfa_offset 16
	mov	QWORD PTR rbl_owned_strings[rip], 0
	pop	r12
	.cfi_def_cfa_offset 8
	mov	QWORD PTR rbl_owned_strings_len[rip], 0
	mov	QWORD PTR rbl_owned_strings_cap[rip], 0
	ret
	.cfi_endproc
.LFE54:
	.size	main, .-main
	.local	rbl_owned_strings_cap
	.comm	rbl_owned_strings_cap,8,8
	.local	rbl_owned_strings_len
	.comm	rbl_owned_strings_len,8,8
	.local	rbl_owned_strings
	.comm	rbl_owned_strings,8,8
	.ident	"GCC: (Debian 14.2.0-19) 14.2.0"
	.section	.note.GNU-stack,"",@progbits
