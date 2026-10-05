.intel_syntax noprefix
.global _start
.section .text
_start:
    xor rax, rax
    xor rcx, rcx
.Lloop:
    add rax, rcx
    inc rcx
    cmp rcx, 100000001
    jne .Lloop
    mov rdx, 5000000050000000
    cmp rax, rdx
    jne .Lbad
    xor edi, edi
    mov eax, 60
    syscall
.Lbad:
    mov edi, 1
    mov eax, 60
    syscall
