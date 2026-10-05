# RBL direct x86-64 ASM generated from /tmp/RBLStudio/examples/test.rbl
.intel_syntax noprefix
.text

.type fn_0,@function
fn_0:
    push rbp
    mov rbp,rsp
    push r12
    push r13
    sub rsp,48
    mov QWORD PTR [rbp-24],0
    mov QWORD PTR [rbp-48],0
    mov r12,rdi
    mov r13,rsi
    lea rdi,[rip+.Lrbl_str_0]
    mov esi,2
    mov edx,r13d
    call rbl_check_arity
    lea rdi,[r12+0]
    lea rsi,[rip+.Lrbl_str_1]
    lea rdx,[rip+.Lrbl_str_2]
    lea rcx,[rip+.Lrbl_str_0]
    call rbl_check_type
    mov rax,[r12+0]
    mov [rbp-24+8],rax
    mov rdx,[r12+8]
    mov [rbp-24+16],rdx
    mov QWORD PTR [rbp-24],1
    lea rdi,[r12+16]
    lea rsi,[rip+.Lrbl_str_1]
    lea rdx,[rip+.Lrbl_str_3]
    lea rcx,[rip+.Lrbl_str_0]
    call rbl_check_type
    mov rax,[r12+16]
    mov [rbp-48+8],rax
    mov rdx,[r12+24]
    mov [rbp-48+16],rdx
    mov QWORD PTR [rbp-48],1
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_get_slot
    sub rsp,16
    mov [rsp],rax
    mov [rsp+8],rdx
    lea rdi,[rbp-48]
    lea rsi,[rip+.Lrbl_str_3]
    call rbl_get_slot
    mov r8,rax
    mov r9,rdx
    mov rdi,[rsp]
    mov rsi,[rsp+8]
    mov rdx,r8
    mov rcx,r9
    add rsp,16
    call rbl_add
    jmp .L_return_0
    xor eax,eax
    xor edx,edx
.L_return_0:
    lea rsp,[rbp-16]
    pop r13
    pop r12
    pop rbp
    ret

.type fn_1,@function
fn_1:
    push rbp
    mov rbp,rsp
    push r12
    push r13
    sub rsp,96
    mov QWORD PTR [rbp-24],0
    mov QWORD PTR [rbp-48],0
    mov QWORD PTR [rbp-72],0
    mov QWORD PTR [rbp-96],0
    mov r12,rdi
    mov r13,rsi
    lea rdi,[rip+.Lrbl_str_4]
    mov esi,0
    mov edx,r13d
    call rbl_check_arity
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_require_unbound
    mov eax,1
    mov rdx,10
    mov [rbp-24+8],rax
    mov [rbp-24+16],rdx
    mov QWORD PTR [rbp-24],1
    sub rsp, 16
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_get_slot
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_5]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    mov eax,1
    mov rdx,20
    sub rsp,16
    mov [rsp],rax
    mov [rsp+8],rdx
    lea rdi,[rbp-24]
    mov rsi,[rsp]
    lea rdx,[rip+.Lrbl_str_2]
    call rbl_require_rebind
    mov rax,[rsp]
    mov rdx,[rsp+8]
    add rsp,16
    mov [rbp-24+8],rax
    mov [rbp-24+16],rdx
    mov QWORD PTR [rbp-24],1
    sub rsp, 16
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_get_slot
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_6]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_get_slot
    sub rsp,16
    mov [rsp],rax
    mov [rsp+8],rdx
    mov eax,1
    mov rdx,15
    mov r8,rax
    mov r9,rdx
    mov rdi,[rsp]
    mov rsi,[rsp+8]
    mov rdx,r8
    mov rcx,r9
    add rsp,16
    call rbl_gt
    mov rdi,rax
    mov rsi,rdx
    call rbl_expect_bool
    test rdx,rdx
    jz .L_if_next_1
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_7]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    jmp .L_if_end_0
.L_if_next_1:
    lea rdi,[rbp-24]
    lea rsi,[rip+.Lrbl_str_2]
    call rbl_get_slot
    sub rsp,16
    mov [rsp],rax
    mov [rsp+8],rdx
    mov eax,1
    mov rdx,15
    mov r8,rax
    mov r9,rdx
    mov rdi,[rsp]
    mov rsi,[rsp+8]
    mov rdx,r8
    mov rcx,r9
    add rsp,16
    call rbl_eq
    mov rdi,rax
    mov rsi,rdx
    call rbl_expect_bool
    test rdx,rdx
    jz .L_if_next_2
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_8]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    jmp .L_if_end_0
.L_if_next_2:
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_9]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
.L_if_end_0:
    sub rsp,32
    mov eax,1
    mov rdx,0
    mov rdi,rax
    mov rsi,rdx
    call rbl_expect_int
    mov [rsp],rdx
    mov eax,1
    mov rdx,10
    mov rdi,rax
    mov rsi,rdx
    call rbl_expect_int
    mov [rsp+8],rdx
    mov rax,[rsp+8]
    mov [rsp+16],rax
    mov rax,[rsp]
    mov [rsp+24],rax
.L_for_3:
    mov rax,[rsp+24]
    cmp rax,[rsp+16]
    jg .L_for_end_4
    mov eax,1
    mov rdx,[rsp+24]
    mov [rbp-48+8],rax
    mov [rbp-48+16],rdx
    mov QWORD PTR [rbp-48],1
    sub rsp, 16
    lea rdi,[rbp-48]
    lea rsi,[rip+.Lrbl_str_10]
    call rbl_get_slot
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    mov rdi,[rsp+24]
    call rbl_inc_i64
    mov [rsp+24],rax
    jmp .L_for_3
.L_for_end_4:
    add rsp,32
    lea rdi,[rbp-72]
    lea rsi,[rip+.Lrbl_str_11]
    call rbl_require_unbound
    mov eax,4
    mov edx,1
    mov [rbp-72+8],rax
    mov [rbp-72+16],rdx
    mov QWORD PTR [rbp-72],1
    lea rdi,[rbp-72]
    lea rsi,[rip+.Lrbl_str_11]
    call rbl_get_slot
    call rbl_not
    mov rdi,rax
    mov rsi,rdx
    call rbl_expect_bool
    test rdx,rdx
    jz .L_if_next_6
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_12]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    mov edx,2
    call rbl_log_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    jmp .L_if_end_5
.L_if_next_6:
    sub rsp, 16
    mov eax,3
    lea rdx,[rip+.Lrbl_str_13]
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    mov edx,1
    call rbl_log_values
    add rsp,16
    xor eax,eax
    xor edx,edx
.L_if_end_5:
    lea rdi,[rbp-96]
    lea rsi,[rip+.Lrbl_str_14]
    call rbl_require_unbound
    lea rdi,[rip+.Lrbl_str_0]
    mov esi,2
    mov edx,2
    call rbl_check_arity
    sub rsp, 32
    mov eax,1
    mov rdx,2
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov eax,1
    mov rdx,3
    mov QWORD PTR [rsp+16], rax
    mov QWORD PTR [rsp+24], rdx
    mov rdi,rsp
    mov esi,2
    call fn_0
    add rsp,32
    mov [rbp-96+8],rax
    mov [rbp-96+16],rdx
    mov QWORD PTR [rbp-96],1
    sub rsp, 16
    lea rdi,[rbp-96]
    lea rsi,[rip+.Lrbl_str_14]
    call rbl_get_slot
    mov QWORD PTR [rsp+0], rax
    mov QWORD PTR [rsp+8], rdx
    mov rdi,rsp
    mov esi,1
    call rbl_print_values
    add rsp,16
    xor eax,eax
    xor edx,edx
    xor eax,eax
    xor edx,edx
.L_return_1:
    lea rsp,[rbp-16]
    pop r13
    pop r12
    pop rbp
    ret

.globl _start
.type _start,@function
_start:
    and rsp,-16
    xor edi,edi
    xor esi,esi
    call fn_1
    xor edi,edi
    call rbl_process_exit
    ud2

.section .rodata
.p2align 3
.Lrbl_str_0:
    .byte 0x61, 0x64, 0x64, 0
.Lrbl_str_1:
    .byte 0x69, 0x6E, 0x74, 0
.Lrbl_str_2:
    .byte 0x61, 0
.Lrbl_str_3:
    .byte 0x62, 0
.Lrbl_str_4:
    .byte 0x6D, 0x61, 0x69, 0x6E, 0
.Lrbl_str_5:
    .byte 0x63, 0x68, 0x61, 0x6E, 0x67, 0x65, 0x20, 0x6E, 0x75, 0x6D, 0x62, 0x65, 0x72, 0
.Lrbl_str_6:
    .byte 0x69, 0x66, 0x20, 0x65, 0x6C, 0x69, 0x66, 0x20, 0x65, 0x6C, 0x73, 0x65, 0
.Lrbl_str_7:
    .byte 0x62, 0x69, 0x67, 0
.Lrbl_str_8:
    .byte 0x65, 0x78, 0x61, 0x63, 0x74, 0x6C, 0x79, 0x20, 0x31, 0x35, 0
.Lrbl_str_9:
    .byte 0x73, 0x6D, 0x61, 0x6C, 0x6C, 0
.Lrbl_str_10:
    .byte 0x69, 0
.Lrbl_str_11:
    .byte 0x6F, 0x6B, 0
.Lrbl_str_12:
    .byte 0x73, 0x6F, 0x6D, 0x65, 0x74, 0x68, 0x69, 0x6E, 0x67, 0x20, 0x77, 0x65, 0x6E, 0x74, 0x20, 0x77, 0x72, 0x6F, 0x6E, 0x67, 0
.Lrbl_str_13:
    .byte 0x6A, 0x75, 0x73, 0x74, 0x20, 0x61, 0x20, 0x77, 0x61, 0x72, 0x6E, 0x69, 0x6E, 0x67, 0
.Lrbl_str_14:
    .byte 0x72, 0x65, 0x73, 0x75, 0x6C, 0x74, 0
.section .note.GNU-stack,"",@progbits
