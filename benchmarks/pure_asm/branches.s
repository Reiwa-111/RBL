.intel_syntax noprefix
.global _start
.section .text
_start:
    xor rax, rax
    xor rcx, rcx
.Lloop:
    cmp rcx, 100000000
    je .Ldone
    cmp rcx, 50000000
    jb .Lplus
    dec rax
    jmp .Lnext
.Lplus:
    inc rax
.Lnext:
    inc rcx
    jmp .Lloop
.Ldone:
    cmp rax, 0
    jne .Lbad
    xor edi, edi
    mov eax, 60
    syscall
.Lbad:
    mov edi, 1
    mov eax, 60
    syscall
