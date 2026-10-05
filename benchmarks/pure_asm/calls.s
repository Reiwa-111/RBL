.intel_syntax noprefix
.global _start
.section .text
_start:
    xor rdi, rdi
    xor rcx, rcx
.Lloop:
    cmp rcx, 10000000
    je .Ldone
    call inc_fn
    inc rcx
    jmp .Lloop
.Ldone:
    cmp rdi, 10000000
    jne .Lbad
    xor edi, edi
    mov eax, 60
    syscall
.Lbad:
    mov edi, 1
    mov eax, 60
    syscall
inc_fn:
    lea rax, [rdi + 1]
    mov rdi, rax
    ret
