.intel_syntax noprefix

.equ TAG_UNIT, 0
.equ TAG_INT, 1
.equ TAG_FLOAT, 2
.equ TAG_STR, 3
.equ TAG_BOOL, 4
.equ TAG_LIST, 5
.equ TAG_DICT, 6
.equ TAG_TUPLE, 7
.equ TAG_NULL, 8

.section .rodata
.Lfmt_error:        .asciz "RBL runtime error: %s\n"
.Lfmt_set:          .asciz "RBL runtime error: variable '%s' already declared; use let\n"
.Lfmt_let_missing:  .asciz "RBL runtime error: variable '%s' is not declared; use set first\n"
.Lfmt_let_type:     .asciz "RBL runtime error: let type mismatch for '%s'\n"
.Lfmt_unbound:      .asciz "RBL runtime error: variable '%s' not found\n"
.Lfmt_arity:        .asciz "RBL runtime error: function '%s' expects %d argument(s), got %d\n"
.Lfmt_param:        .asciz "RBL runtime error: parameter '%s' of '%s': expected %s, got %s\n"
.Lfmt_bool:         .asciz "RBL runtime error: condition must be bool, got %s\n"
.Lfmt_int:          .asciz "RBL runtime error: range bound must be int, got %s\n"
.Lfmt_binary:       .asciz "RBL runtime error: invalid operands for %s: %s and %s\n"
.Lfmt_unary:        .asciz "RBL runtime error: invalid operand for %s: %s\n"
.Lfmt_method:       .asciz "RBL runtime error: unknown method '%s.%s'\n"
.Lfmt_function:     .asciz "RBL runtime error: function '%s' not found\n"
.Lfmt_overflow:     .asciz "RBL runtime error: integer overflow\n"
.Lfmt_divzero:      .asciz "RBL runtime error: integer division by zero\n"
.Lfmt_divov:        .asciz "RBL runtime error: integer division overflow\n"
.Lfmt_print_int:    .asciz "%lld"
.Lfmt_print_float:  .asciz "%.17g"
.Lfmt_print_str:    .asciz "%s"
.Lfmt_print_space:  .asciz " "
.Lfmt_print_nl:     .asciz "\n"
.Lstr_true:         .asciz "true"
.Lstr_false:        .asciz "false"
.Lstr_int:          .asciz "int"
.Lstr_float:        .asciz "float"
.Lstr_string:       .asciz "string"
.Lstr_bool:         .asciz "bool"
.Lstr_list:         .asciz "list"
.Lstr_dict:         .asciz "dict"
.Lstr_tuple:        .asciz "tuple"
.Lstr_null:         .asciz "null"
.Lstr_unit:         .asciz "()"
.Lstr_unit_empty:   .asciz ""
.Lstr_add:          .asciz "+"
.Lstr_sub:          .asciz "-"
.Lstr_mul:          .asciz "*"
.Lstr_div:          .asciz "/"
.Lstr_mod:          .asciz "%"
.Lstr_eq:           .asciz "=="
.Lstr_ne:           .asciz "!="
.Lstr_lt:           .asciz "<"
.Lstr_gt:           .asciz ">"
.Lstr_le:           .asciz "<="
.Lstr_ge:           .asciz ">="
.Lstr_and:          .asciz "and"
.Lstr_or:           .asciz "or"
.Lstr_neg:          .asciz "-"
.Lstr_not:          .asciz "not"
.Lmsg_range_value:  .asciz "range is only valid in for"
.Llog_warn_prefix:  .asciz "[WARN] "
.Llog_error_prefix: .asciz "[ERROR] "

.Lmsg_len_type:      .asciz "len() expects a string"
.Lmsg_input_type:    .asciz "input() prompt must be a string"
.Lmsg_read_type:     .asciz "read_file() path must be a string"
.Lmsg_write_type:    .asciz "write_file() expects string path and string content"
.Lmsg_numeric:       .asciz "numeric argument required"
.Lmsg_abs_type:      .asciz "abs() expects int or float"
.Lmsg_sqrt_type:     .asciz "sqrt() expects int or float"
.Lmsg_min_type:      .asciz "min/max arguments must all be int or all be float"
.Lmsg_min_empty:     .asciz "min/max require at least one argument"
.Lmsg_int_type:      .asciz "int() cannot convert this value"
.Lmsg_float_type:    .asciz "float() cannot convert this value"
.Lmsg_str_type:      .asciz "str() cannot convert this value"
.Lmsg_input_eof:     .asciz "input() failed to read stdin"
.Lmsg_read_open:     .asciz "read_file() failed to open file"
.Lmsg_read_io:       .asciz "read_file() failed while reading file"
.Lmsg_write_open:    .asciz "write_file() failed to open file"
.Lmsg_write_io:      .asciz "write_file() failed while writing file"
.Lmsg_convert_range: .asciz "numeric conversion out of range"
.Lmode_rb:           .asciz "rb"
.Lmode_wb:           .asciz "wb"
.Lfmt_str_int:       .asciz "%lld"
.Lfmt_str_float:     .asciz "%.17g"
.align 8
.Lconst_sign:       .quad 0x8000000000000000

.section .text
.extern printf
.extern fprintf
.extern exit
.extern malloc
.extern strlen
.extern memcpy
.extern strcmp
.extern stderr
.extern stdin
.extern stdout
.extern fgets
.extern getline
.extern fflush
.extern fopen
.extern fclose
.extern fread
.extern fwrite
.extern fseek
.extern ftell
.extern malloc
.extern free
.extern strtoll
.extern strtod
.extern snprintf
.extern toupper
.extern tolower
.extern strstr
.extern strncmp
.extern access
.extern remove
.extern getcwd
.extern clock_gettime
.extern nanosleep
.extern time
.extern srand
.extern rand
.extern sin
.extern cos
.extern tan
.extern log
.extern exp
.extern floor
.extern ceil
.extern round

.globl rbl_process_exit
.type rbl_process_exit,@function
rbl_process_exit:
    call exit
    ud2

.globl rbl_type_name
.type rbl_type_name,@function
rbl_type_name:
    cmp rdi, TAG_INT
    je .Ltn_int
    cmp rdi, TAG_FLOAT
    je .Ltn_float
    cmp rdi, TAG_STR
    je .Ltn_str
    cmp rdi, TAG_BOOL
    je .Ltn_bool
    cmp rdi, TAG_LIST
    je .Ltn_list
    cmp rdi, TAG_DICT
    je .Ltn_dict
    cmp rdi, TAG_TUPLE
    je .Ltn_tuple
    cmp rdi, TAG_NULL
    je .Ltn_null
    lea rax, [rip+.Lstr_unit]
    ret
.Ltn_int:
    lea rax, [rip+.Lstr_int]
    ret
.Ltn_float:
    lea rax, [rip+.Lstr_float]
    ret
.Ltn_str:
    lea rax, [rip+.Lstr_string]
    ret
.Ltn_bool:
    lea rax, [rip+.Lstr_bool]
    ret
.Ltn_list:
    lea rax, [rip+.Lstr_list]
    ret
.Ltn_dict:
    lea rax, [rip+.Lstr_dict]
    ret
.Ltn_tuple:
    lea rax, [rip+.Lstr_tuple]
    ret
.Ltn_null:
    lea rax, [rip+.Lstr_null]
    ret

.globl rbl_fail
.type rbl_fail,@function
rbl_fail:
    push rbp
    mov rbp, rsp
    mov rdx, rdi
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_error]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_require_unbound
rbl_require_unbound:
    cmp QWORD PTR [rdi], 0
    jne .Lset_twice
    ret
.Lset_twice:
    mov r8, rsi
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_set]
    mov rdx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_get_slot
rbl_get_slot:
    cmp QWORD PTR [rdi], 0
    je .Lget_unbound
    mov rax, QWORD PTR [rdi+8]
    mov rdx, QWORD PTR [rdi+16]
    ret
.Lget_unbound:
    mov r8, rsi
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_unbound]
    mov rdx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_require_rebind
rbl_require_rebind:
    cmp QWORD PTR [rdi], 0
    je .Llet_missing
    mov r8, QWORD PTR [rdi+8]
    cmp r8, rsi
    jne .Llet_type
    ret
.Llet_missing:
    mov r8, rdx
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_let_missing]
    mov rdx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2
.Llet_type:
    mov r8, rdx
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_let_type]
    mov rdx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_check_arity
rbl_check_arity:
    cmp esi, edx
    jne .Larity_bad
    ret
.Larity_bad:
    mov r8, rdi
    mov r9d, esi
    mov r10d, edx
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_arity]
    mov rdx, r8
    mov ecx, r9d
    mov r8d, r10d
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_check_type
rbl_check_type:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rsi
    mov [rbp-16], rdx
    mov [rbp-24], rcx
    mov rax, [rdi]
    mov [rbp-32], rax
    mov rdi, rax
    call rbl_type_name
    mov [rbp-40], rax

    mov rdi, [rbp-32]
    cmp rdi, TAG_INT
    je .Lct_actual_int
    cmp rdi, TAG_FLOAT
    je .Lct_actual_float
    cmp rdi, TAG_STR
    je .Lct_actual_str
    cmp rdi, TAG_BOOL
    je .Lct_actual_bool
    lea r8, [rip+.Lstr_unit]
    jmp .Lct_compare
.Lct_actual_int:
    lea r8, [rip+.Lstr_int]
    jmp .Lct_compare
.Lct_actual_float:
    lea r8, [rip+.Lstr_float]
    jmp .Lct_compare
.Lct_actual_str:
    lea r8, [rip+.Lstr_string]
    jmp .Lct_compare
.Lct_actual_bool:
    lea r8, [rip+.Lstr_bool]
.Lct_compare:
    mov rdi, [rbp-8]
    mov rsi, r8
    call strcmp
    test eax, eax
    je .Lct_ok

    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_param]
    mov rdx, [rbp-16]
    mov rcx, [rbp-24]
    mov r8, [rbp-8]
    mov r9, [rbp-40]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2
.Lct_ok:
    leave
    ret

.globl rbl_expect_bool
rbl_expect_bool:
    cmp rdi, TAG_BOOL
    jne .Lexpect_bool_bad
    mov eax, TAG_BOOL
    ret
.Lexpect_bool_bad:
    push rbp
    mov rbp, rsp
    sub rsp, 8
    mov [rbp-8], rdi
    mov rdi, [rbp-8]
    call rbl_type_name
    mov rdx, rax
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_bool]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_expect_int
rbl_expect_int:
    cmp rdi, TAG_INT
    jne .Lexpect_int_bad
    mov eax, TAG_INT
    ret
.Lexpect_int_bad:
    push rbp
    mov rbp, rsp
    sub rsp, 8
    mov [rbp-8], rdi
    mov rdi, [rbp-8]
    call rbl_type_name
    mov rdx, rax
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_int]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_unknown_function
rbl_unknown_function:
    mov r8, rdi
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_function]
    mov rdx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_unknown_method
rbl_unknown_method:
    mov r8, rdi
    mov r9, rsi
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_method]
    mov rdx, r8
    mov rcx, r9
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_bad_binary
rbl_bad_binary:
    push rbp
    mov rbp, rsp
    sub rsp, 40
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov [rbp-24], rdx
    mov rdi, [rbp-16]
    call rbl_type_name
    mov [rbp-32], rax
    mov rdi, [rbp-24]
    call rbl_type_name
    mov [rbp-40], rax
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_binary]
    mov rdx, [rbp-8]
    mov rcx, [rbp-32]
    mov r8, [rbp-40]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_bad_unary
rbl_bad_unary:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov rdi, [rbp-16]
    call rbl_type_name
    mov r8, rax
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_unary]
    mov rdx, [rbp-8]
    mov rcx, r8
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_int_overflow
rbl_int_overflow:
    jmp .Loverflow
.globl rbl_int_divzero
rbl_int_divzero:
    jmp .Ldiv_zero
.globl rbl_int_div_overflow
rbl_int_div_overflow:
    jmp .Ldiv_overflow

.globl rbl_inc_i64
rbl_inc_i64:
    mov rax, rdi
    add rax, 1
    jo .Loverflow
    ret
.globl rbl_dec_i64
rbl_dec_i64:
    mov rax, rdi
    sub rax, 1
    jo .Loverflow
    ret
.Loverflow:
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_overflow]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

.globl rbl_neg
rbl_neg:
    /* Unary helpers take (tag in rdi, payload in rsi) like every other runtime
       entry point. The value registers are reloaded here so the frame below can
       keep using rax/rdx unchanged. */
    mov rax, rdi
    mov rdx, rsi
    cmp rax, TAG_INT
    je .Lneg_int
    cmp rax, TAG_FLOAT
    je .Lneg_float
    mov rsi, rax
    lea rdi, [rip+.Lstr_neg]
    jmp rbl_bad_unary
.Lneg_int:
    mov rax, 0x8000000000000000
    cmp rdx, rax
    je .Loverflow
    neg rdx
    mov eax, TAG_INT
    ret
.Lneg_float:
    mov rcx, QWORD PTR [rip+.Lconst_sign]
    xor rdx, rcx
    mov eax, TAG_FLOAT
    ret

.globl rbl_not
rbl_not:
    /* Same (rdi, rsi) convention as rbl_neg. */
    mov rax, rdi
    mov rdx, rsi
    cmp rax, TAG_BOOL
    jne .Lnot_bad
    xor edx, 1
    mov eax, TAG_BOOL
    ret
.Lnot_bad:
    mov rsi, rax
    lea rdi, [rip+.Lstr_not]
    jmp rbl_bad_unary

.globl rbl_add
rbl_add:
    cmp rdi, TAG_INT
    je .Ladd_int
    cmp rdi, TAG_FLOAT
    je .Ladd_float
    cmp rdi, TAG_STR
    je .Ladd_str
    jmp .Ladd_bad
.Ladd_int:
    cmp rdx, TAG_INT
    jne .Ladd_bad
    mov rax, rsi
    add rax, rcx
    jo .Loverflow
    mov rdx, rax
    mov eax, TAG_INT
    ret
.Ladd_float:
    cmp rdx, TAG_FLOAT
    jne .Ladd_bad
    movq xmm0, rsi
    movq xmm1, rcx
    addsd xmm0, xmm1
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Ladd_str:
    cmp rdx, TAG_STR
    jne .Ladd_bad
    mov rdi, rsi
    mov rsi, rcx
    call rbl_str_concat
    mov rdx, rax
    mov eax, TAG_STR
    ret
.Ladd_bad:
    # original tags are still in rdi/rdx here
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_add]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_sub
rbl_sub:
    cmp rdi, TAG_INT
    je .Lsub_int
    cmp rdi, TAG_FLOAT
    je .Lsub_float
    jmp .Lsub_bad
.Lsub_int:
    cmp rdx, TAG_INT
    jne .Lsub_bad
    mov rax, rsi
    sub rax, rcx
    jo .Loverflow
    mov rdx, rax
    mov eax, TAG_INT
    ret
.Lsub_float:
    cmp rdx, TAG_FLOAT
    jne .Lsub_bad
    movq xmm0, rsi
    movq xmm1, rcx
    subsd xmm0, xmm1
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lsub_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_sub]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_mul
rbl_mul:
    cmp rdi, TAG_INT
    je .Lmul_int
    cmp rdi, TAG_FLOAT
    je .Lmul_float
    jmp .Lmul_bad
.Lmul_int:
    cmp rdx, TAG_INT
    jne .Lmul_bad
    mov rax, rsi
    imul rax, rcx
    jo .Loverflow
    mov rdx, rax
    mov eax, TAG_INT
    ret
.Lmul_float:
    cmp rdx, TAG_FLOAT
    jne .Lmul_bad
    movq xmm0, rsi
    movq xmm1, rcx
    mulsd xmm0, xmm1
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lmul_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_mul]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_div
rbl_div:
    cmp rdi, TAG_INT
    je .Ldiv_int
    cmp rdi, TAG_FLOAT
    je .Ldiv_float
    jmp .Ldiv_bad
.Ldiv_int:
    cmp rdx, TAG_INT
    jne .Ldiv_bad
    test rcx, rcx
    je .Ldiv_zero
    mov rax, 0x8000000000000000
    cmp rsi, rax
    jne .Ldiv_do
    cmp rcx, -1
    je .Ldiv_overflow
.Ldiv_do:
    mov rax, rsi
    cqo
    idiv rcx
    mov rdx, rax
    mov eax, TAG_INT
    ret
.Ldiv_float:
    cmp rdx, TAG_FLOAT
    jne .Ldiv_bad
    movq xmm0, rsi
    movq xmm1, rcx
    divsd xmm0, xmm1
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Ldiv_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_div]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary
.Ldiv_zero:
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_divzero]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2
.Ldiv_overflow:
    mov rdi, QWORD PTR [rip+stderr]
    lea rsi, [rip+.Lfmt_divov]
    xor eax, eax
    call fprintf
    mov edi, 1
    call exit
    ud2

/* Integer remainder. Same conventions as rbl_div: (lt in rdi, lp in rsi,
   rt in rdx, rp in rcx) -> (tag in rax, payload in rdx). `%` is integer only;
   float operands fall through to the invalid-operands diagnostic. Zero divisor
   and INT64_MIN % -1 reuse the division error paths so both backends and both
   arithmetic forms report the same messages. */
.globl rbl_mod
rbl_mod:
    cmp rdi, TAG_INT
    je .Lmod_int
    jmp .Lmod_bad
.Lmod_int:
    cmp rdx, TAG_INT
    jne .Lmod_bad
    test rcx, rcx
    je .Ldiv_zero
    mov rax, 0x8000000000000000
    cmp rsi, rax
    jne .Lmod_do
    cmp rcx, -1
    je .Ldiv_overflow
.Lmod_do:
    mov rax, rsi
    cqo
    idiv rcx
    mov eax, TAG_INT
    ret
.Lmod_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_mod]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_eq
rbl_eq:
    cmp rdi, rdx
    jne .Leq_false
    cmp rdi, TAG_INT
    je .Leq_int
    cmp rdi, TAG_FLOAT
    je .Leq_float
    cmp rdi, TAG_STR
    je .Leq_str
    cmp rdi, TAG_BOOL
    je .Leq_bool
    cmp rdi, TAG_LIST
    je rbl_list_eq
    cmp rdi, TAG_DICT
    je rbl_dict_eq
    cmp rdi, TAG_TUPLE
    je rbl_tuple_eq
    mov edx, 1
    mov eax, TAG_BOOL
    ret
.Leq_int:
    cmp rsi, rcx
    sete dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Leq_bool:
    cmp rsi, rcx
    sete dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Leq_str:
    push rbp
    mov rbp, rsp
    mov rdi, rsi
    mov rsi, rcx
    call strcmp
    test eax, eax
    sete dl
    movzx edx, dl
    mov eax, TAG_BOOL
    leave
    ret
.Leq_float:
    movq xmm0, rsi
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    sete al
    setnp dl
    and al, dl
    movzx edx, al
    mov eax, TAG_BOOL
    ret
.Leq_false:
    xor edx, edx
    mov eax, TAG_BOOL
    ret

.globl rbl_ne
rbl_ne:
    call rbl_eq
    xor edx, 1
    ret

.globl rbl_lt
rbl_lt:
    cmp rdi, TAG_INT
    je .Llt_int
    cmp rdi, TAG_FLOAT
    je .Llt_float
    jmp .Llt_bad
.Llt_int:
    cmp rdx, TAG_INT
    jne .Llt_bad
    cmp rsi, rcx
    setl dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Llt_float:
    cmp rdx, TAG_FLOAT
    jne .Llt_bad
    movq xmm0, rsi
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    setb al
    setnp dl
    and al, dl
    movzx edx, al
    mov eax, TAG_BOOL
    ret
.Llt_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_lt]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_gt
rbl_gt:
    cmp rdi, TAG_INT
    je .Lgt_int
    cmp rdi, TAG_FLOAT
    je .Lgt_float
    jmp .Lgt_bad
.Lgt_int:
    cmp rdx, TAG_INT
    jne .Lgt_bad
    cmp rsi, rcx
    setg dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Lgt_float:
    cmp rdx, TAG_FLOAT
    jne .Lgt_bad
    movq xmm0, rsi
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    seta al
    setnp dl
    and al, dl
    movzx edx, al
    mov eax, TAG_BOOL
    ret
.Lgt_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_gt]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_le
rbl_le:
    cmp rdi, TAG_INT
    je .Lle_int
    cmp rdi, TAG_FLOAT
    je .Lle_float
    jmp .Lle_bad
.Lle_int:
    cmp rdx, TAG_INT
    jne .Lle_bad
    cmp rsi, rcx
    setle dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Lle_float:
    cmp rdx, TAG_FLOAT
    jne .Lle_bad
    movq xmm0, rsi
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    setbe al
    setnp dl
    and al, dl
    movzx edx, al
    mov eax, TAG_BOOL
    ret
.Lle_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_le]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_ge
rbl_ge:
    cmp rdi, TAG_INT
    je .Lge_int
    cmp rdi, TAG_FLOAT
    je .Lge_float
    jmp .Lge_bad
.Lge_int:
    cmp rdx, TAG_INT
    jne .Lge_bad
    cmp rsi, rcx
    setge dl
    movzx edx, dl
    mov eax, TAG_BOOL
    ret
.Lge_float:
    cmp rdx, TAG_FLOAT
    jne .Lge_bad
    movq xmm0, rsi
    movq xmm1, rcx
    ucomisd xmm0, xmm1
    setae al
    setnp dl
    and al, dl
    movzx edx, al
    mov eax, TAG_BOOL
    ret
.Lge_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_ge]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_and
rbl_and:
    cmp rdi, TAG_BOOL
    jne .Land_bad
    cmp rdx, TAG_BOOL
    jne .Land_bad
    and rsi, rcx
    mov rdx, rsi
    mov eax, TAG_BOOL
    ret
.Land_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_and]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_or
rbl_or:
    cmp rdi, TAG_BOOL
    jne .Lor_bad
    cmp rdx, TAG_BOOL
    jne .Lor_bad
    or rsi, rcx
    mov rdx, rsi
    mov eax, TAG_BOOL
    ret
.Lor_bad:
    mov r8, rdi
    mov r9, rdx
    lea rdi, [rip+.Lstr_or]
    mov rsi, r8
    mov rdx, r9
    jmp rbl_bad_binary

.globl rbl_str_concat
rbl_str_concat:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    call strlen
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    call strlen
    mov [rbp-32], rax
    mov rdi, [rbp-24]
    add rdi, [rbp-32]
    jo .Loverflow
    add rdi, 1
    jc .Loverflow
    call malloc
    test rax, rax
    jz .Lmalloc_bad
    mov [rbp-40], rax
    mov rdi, rax
    mov rsi, [rbp-8]
    mov rdx, [rbp-24]
    call memcpy
    mov rdi, [rbp-40]
    add rdi, [rbp-24]
    mov rsi, [rbp-16]
    mov rdx, [rbp-32]
    call memcpy
    mov rax, [rbp-40]
    mov rcx, [rbp-24]
    add rcx, [rbp-32]
    mov byte ptr [rax+rcx], 0
    leave
    ret
.Lmalloc_bad:
    lea rdi, [rip+.Lfmt_error]
    # reuse generic runtime error path with a static literal built below
    lea rdi, [rip+.Lmsg_oom]
    call rbl_fail
    ud2

.section .rodata
.Lmsg_oom: .asciz "out of memory while concatenating strings"
.section .text

.globl rbl_print_one
rbl_print_one:
    push rbp
    mov rbp, rsp
    cmp rdi, TAG_INT
    je .Lp_int
    cmp rdi, TAG_FLOAT
    je .Lp_float
    cmp rdi, TAG_STR
    je .Lp_str
    cmp rdi, TAG_BOOL
    je .Lp_bool
    cmp rdi, TAG_LIST
    je .Lp_list
    cmp rdi, TAG_DICT
    je .Lp_dict
    cmp rdi, TAG_TUPLE
    je .Lp_tuple
    cmp rdi, TAG_NULL
    je .Lp_null
    lea rdi, [rip+.Lstr_unit_empty]
    xor eax, eax
    call printf
    leave
    ret
.Lp_int:
    mov rdx, rsi
    lea rdi, [rip+.Lfmt_print_int]
    mov rsi, rdx
    xor eax, eax
    call printf
    leave
    ret
.Lp_float:
    movq xmm0, rsi
    lea rdi, [rip+.Lfmt_print_float]
    mov eax, 1
    call printf
    leave
    ret
.Lp_str:
    mov rdx, rsi
    lea rdi, [rip+.Lfmt_print_str]
    mov rsi, rdx
    xor eax, eax
    call printf
    leave
    ret
.Lp_bool:
    test rsi, rsi
    jz .Lp_false
    lea rdi, [rip+.Lstr_true]
    xor eax, eax
    call printf
    leave
    ret
.Lp_false:
    lea rdi, [rip+.Lstr_false]
    xor eax, eax
    call printf
    leave
    ret
.Lp_list:
    call rbl_list_str
    mov rsi, rdx
    lea rdi, [rip+.Lfmt_print_str]
    xor eax, eax
    call printf
    leave
    ret
.Lp_dict:
    call rbl_dict_str
    mov rsi, rdx
    lea rdi, [rip+.Lfmt_print_str]
    xor eax, eax
    call printf
    leave
    ret
.Lp_tuple:
    call rbl_tuple_str
    mov rsi, rdx
    lea rdi, [rip+.Lfmt_print_str]
    xor eax, eax
    call printf
    leave
    ret
.Lp_null:
    lea rdi, [rip+.Lfmt_print_str]
    lea rsi, [rip+.Lstr_null]
    xor eax, eax
    call printf
    leave
    ret

.globl rbl_print_values
rbl_print_values:
    push rbp
    mov rbp, rsp
    push r12
    push r13
    push r14
    sub rsp, 8
    mov r12, rdi
    mov r13, rsi
    xor r14d, r14d
.Lpv_loop:
    cmp r14, r13
    jae .Lpv_done
    mov rax, r14
    imul rax, 16
    mov rdi, [r12+rax]
    mov rsi, [r12+rax+8]
    call rbl_print_one
    inc r14
    cmp r14, r13
    jae .Lpv_done
    lea rdi, [rip+.Lfmt_print_space]
    xor eax, eax
    call printf
    jmp .Lpv_loop
.Lpv_done:
    lea rdi, [rip+.Lfmt_print_nl]
    xor eax, eax
    call printf
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    ret

.globl rbl_log_values
rbl_log_values:
    push rbp
    mov rbp, rsp
    push r12
    push r13
    mov r12, rdi
    mov r13, rsi
    cmp edx, 1
    je .Llog_warn
    lea rdi, [rip+.Llog_error_prefix]
    jmp .Llog_print
.Llog_warn:
    lea rdi, [rip+.Llog_warn_prefix]
.Llog_print:
    xor eax, eax
    call printf
    mov rdi, r12
    mov rsi, r13
    call rbl_print_values
    pop r13
    pop r12
    pop rbp
    ret


/* ============================ RBL STDLIB ============================ */

.globl rbl_len
rbl_len:
    cmp rdi, TAG_LIST
    je rbl_list_len
    cmp rdi, TAG_DICT
    je rbl_dict_len
    cmp rdi, TAG_TUPLE
    je rbl_tuple_len
    cmp rdi, TAG_STR
    jne .Llen_bad
    mov rcx, rsi
    xor rdx, rdx
.Llen_loop:
    mov al, byte ptr [rcx]
    test al, al
    jz .Llen_done
    mov ah, al
    and ah, 0xC0
    cmp ah, 0x80
    je .Llen_next
    inc rdx
.Llen_next:
    inc rcx
    jmp .Llen_loop
.Llen_done:
    mov eax, TAG_INT
    ret
.Llen_bad:
    lea rdi, [rip+.Lmsg_len_type]
    jmp rbl_fail

.globl rbl_input0
rbl_input0:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov QWORD PTR [rbp-8], 0
    mov QWORD PTR [rbp-16], 0
    lea rdi, [rbp-8]
    lea rsi, [rbp-16]
    mov rdx, QWORD PTR [rip+stdin]
    call getline
    cmp rax, -1
    je .Linput_fail
    mov rdi, [rbp-8]
    call rbl_trim_newline
    mov rdx, rax
    mov eax, TAG_STR
    leave
    ret
.Linput_fail:
    lea rdi, [rip+.Lmsg_input_eof]
    call rbl_fail
    ud2

.globl rbl_input1
rbl_input1:
    cmp rdi, TAG_STR
    jne .Linput_type
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rsi
    mov rdi, rsi
    call rbl_print_raw_string
    mov rdi, QWORD PTR [rip+stdout]
    call fflush
    mov QWORD PTR [rbp-16], 0
    lea rdi, [rbp-8]
    lea rsi, [rbp-16]
    mov rdx, QWORD PTR [rip+stdin]
    call getline
    cmp rax, -1
    je .Linput_fail1
    mov rdi, [rbp-8]
    call rbl_trim_newline
    mov rdx, rax
    mov eax, TAG_STR
    leave
    ret
.Linput_type:
    lea rdi, [rip+.Lmsg_input_type]
    jmp rbl_fail
.Linput_fail1:
    lea rdi, [rip+.Lmsg_input_eof]
    call rbl_fail
    ud2

.type rbl_print_raw_string,@function
rbl_print_raw_string:
    push rbp
    mov rbp, rsp
    mov rsi, rdi
    lea rdi, [rip+.Lfmt_print_str]
    xor eax, eax
    call printf
    leave
    ret

.type rbl_trim_newline,@function
rbl_trim_newline:
    mov rcx, rdi
.Ltrim_loop:
    mov al, byte ptr [rcx]
    test al, al
    jz .Ltrim_done
    cmp al, 10
    je .Ltrim_zero
    cmp al, 13
    je .Ltrim_zero
    inc rcx
    jmp .Ltrim_loop
.Ltrim_zero:
    mov byte ptr [rcx], 0
.Ltrim_done:
    mov rax, rdi
    ret

.globl rbl_read_file
rbl_read_file:
    cmp rdi, TAG_STR
    jne .Lread_type
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov rdi, rsi
    lea rsi, [rip+.Lmode_rb]
    call fopen
    test rax, rax
    jz .Lread_open_bad
    mov [rbp-8], rax
    mov rdi, rax
    xor esi, esi
    mov edx, 2
    call fseek
    test eax, eax
    jne .Lread_io_bad
    mov rdi, [rbp-8]
    call ftell
    cmp rax, -1
    je .Lread_io_bad
    mov [rbp-16], rax
    mov rdi, [rbp-8]
    xor esi, esi
    mov edx, 0
    call fseek
    test eax, eax
    jne .Lread_io_bad
    mov rdi, [rbp-16]
    add rdi, 1
    jc .Lread_io_bad
    call malloc
    test rax, rax
    jz .Lread_io_bad
    mov [rbp-24], rax
    mov rdi, rax
    mov esi, 1
    mov rdx, [rbp-16]
    mov rcx, [rbp-8]
    call fread
    cmp rax, [rbp-16]
    jne .Lread_buf_bad
    mov rax, [rbp-24]
    mov rdx, [rbp-16]
    mov byte ptr [rax+rdx], 0
    mov rdi, [rbp-8]
    call fclose
    mov rdx, [rbp-24]
    mov eax, TAG_STR
    leave
    ret
.Lread_buf_bad:
    mov rdi, [rbp-24]
    call free
.Lread_io_bad:
    mov rdi, [rbp-8]
    test rdi, rdi
    jz .Lread_io_fail
    call fclose
.Lread_io_fail:
    lea rdi, [rip+.Lmsg_read_io]
    call rbl_fail
    ud2
.Lread_open_bad:
    lea rdi, [rip+.Lmsg_read_open]
    call rbl_fail
    ud2
.Lread_type:
    lea rdi, [rip+.Lmsg_read_type]
    jmp rbl_fail

.globl rbl_write_file_values
rbl_write_file_values:
    cmp esi, 2
    jne .Lwrite_type
    mov rax, [rdi]
    cmp rax, TAG_STR
    jne .Lwrite_type
    mov rax, [rdi+16]
    cmp rax, TAG_STR
    jne .Lwrite_type
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, [rdi+8]
    mov [rbp-8], rax
    mov rax, [rdi+24]
    mov [rbp-16], rax
    mov rdi, [rbp-8]
    lea rsi, [rip+.Lmode_wb]
    call fopen
    test rax, rax
    jz .Lwrite_open_bad
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    call strlen
    mov [rbp-32], rax
    mov rdi, [rbp-16]
    mov esi, 1
    mov rdx, [rbp-32]
    mov rcx, [rbp-24]
    call fwrite
    cmp rax, [rbp-32]
    jne .Lwrite_io_bad
    mov rdi, [rbp-24]
    call fclose
    xor eax, eax
    xor edx, edx
    leave
    ret
.Lwrite_open_bad:
    lea rdi, [rip+.Lmsg_write_open]
    call rbl_fail
    ud2
.Lwrite_io_bad:
    mov rdi, [rbp-24]
    call fclose
    lea rdi, [rip+.Lmsg_write_io]
    call rbl_fail
    ud2
.Lwrite_type:
    lea rdi, [rip+.Lmsg_write_type]
    jmp rbl_fail

.globl rbl_abs
rbl_abs:
    cmp rdi, TAG_INT
    je .Labs_int
    cmp rdi, TAG_FLOAT
    je .Labs_float
    lea rdi, [rip+.Lmsg_abs_type]
    jmp rbl_fail
.Labs_int:
    mov rax, 0x8000000000000000
    cmp rsi, rax
    je .Labs_overflow
    mov rdx, rsi
    neg rdx
    test rsi, rsi
    cmovg rdx, rsi
    mov eax, TAG_INT
    ret
.Labs_float:
    mov rcx, 0x7fffffffffffffff
    mov rdx, rsi
    and rdx, rcx
    mov eax, TAG_FLOAT
    ret
.Labs_overflow:
    lea rdi, [rip+.Lmsg_convert_range]
    jmp rbl_fail

.globl rbl_sqrt
rbl_sqrt:
    cmp rdi, TAG_INT
    je .Lsqr_int
    cmp rdi, TAG_FLOAT
    je .Lsqr_float
    lea rdi, [rip+.Lmsg_sqrt_type]
    jmp rbl_fail
.Lsqr_int:
    test rsi, rsi
    js .Lsqr_negative
    cvtsi2sd xmm0, rsi
    sqrtsd xmm0, xmm0
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lsqr_float:
    movq xmm0, rsi
    pxor xmm1, xmm1
    ucomisd xmm0, xmm1
    jb .Lsqr_negative
    sqrtsd xmm0, xmm0
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lsqr_negative:
    lea rdi, [rip+.Lmsg_numeric]
    jmp rbl_fail

.globl rbl_min_values
rbl_min_values:
    test esi, esi
    jz .Lmin_empty
    push rbp
    mov rbp, rsp
    push r12
    push r13
    push r14
    sub rsp, 8
    mov r12, rdi
    mov r13d, esi
    mov r14, [r12]
    cmp r14, TAG_INT
    je .Lmin_int
    cmp r14, TAG_FLOAT
    je .Lmin_float
    jmp .Lmin_type_bad
.Lmin_int:
    mov r14, [r12+8]
    mov ecx, 1
.Lmin_int_loop:
    cmp ecx, r13d
    jae .Lmin_int_done
    mov rax, rcx
    imul rax, 16
    mov rdx, [r12+rax]
    cmp rdx, TAG_INT
    jne .Lmin_type_bad
    mov rdx, [r12+rax+8]
    cmp rdx, r14
    jge .Lmin_int_next
    mov r14, rdx
.Lmin_int_next:
    inc ecx
    jmp .Lmin_int_loop
.Lmin_int_done:
    mov rdx, r14
    mov eax, TAG_INT
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    ret
.Lmin_float:
    movq xmm0, [r12+8]
    mov ecx, 1
.Lmin_float_loop:
    cmp ecx, r13d
    jae .Lmin_float_done
    mov rax, rcx
    imul rax, 16
    mov rdx, [r12+rax]
    cmp rdx, TAG_FLOAT
    jne .Lmin_type_bad
    mov rdx, [r12+rax+8]
    movq xmm1, rdx
    ucomisd xmm1, xmm0
    jae .Lmin_float_next
    movq xmm0, xmm1
.Lmin_float_next:
    inc ecx
    jmp .Lmin_float_loop
.Lmin_float_done:
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    ret
.Lmin_empty:
    lea rdi, [rip+.Lmsg_min_empty]
    jmp rbl_fail
.Lmin_type_bad:
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    lea rdi, [rip+.Lmsg_min_type]
    jmp rbl_fail

.globl rbl_max_values
rbl_max_values:
    test esi, esi
    jz .Lmax_empty
    push rbp
    mov rbp, rsp
    push r12
    push r13
    push r14
    sub rsp, 8
    mov r12, rdi
    mov r13d, esi
    mov r14, [r12]
    cmp r14, TAG_INT
    je .Lmax_int
    cmp r14, TAG_FLOAT
    je .Lmax_float
    jmp .Lmax_type_bad
.Lmax_int:
    mov r14, [r12+8]
    mov ecx, 1
.Lmax_int_loop:
    cmp ecx, r13d
    jae .Lmax_int_done
    mov rax, rcx
    imul rax, 16
    mov rdx, [r12+rax]
    cmp rdx, TAG_INT
    jne .Lmax_type_bad
    mov rdx, [r12+rax+8]
    cmp rdx, r14
    jle .Lmax_int_next
    mov r14, rdx
.Lmax_int_next:
    inc ecx
    jmp .Lmax_int_loop
.Lmax_int_done:
    mov rdx, r14
    mov eax, TAG_INT
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    ret
.Lmax_float:
    movq xmm0, [r12+8]
    mov ecx, 1
.Lmax_float_loop:
    cmp ecx, r13d
    jae .Lmax_float_done
    mov rax, rcx
    imul rax, 16
    mov rdx, [r12+rax]
    cmp rdx, TAG_FLOAT
    jne .Lmax_type_bad
    mov rdx, [r12+rax+8]
    movq xmm1, rdx
    ucomisd xmm1, xmm0
    jbe .Lmax_float_next
    movq xmm0, xmm1
.Lmax_float_next:
    inc ecx
    jmp .Lmax_float_loop
.Lmax_float_done:
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    ret
.Lmax_empty:
    lea rdi, [rip+.Lmsg_min_empty]
    jmp rbl_fail
.Lmax_type_bad:
    add rsp, 8
    pop r14
    pop r13
    pop r12
    pop rbp
    lea rdi, [rip+.Lmsg_min_type]
    jmp rbl_fail

.globl rbl_int
rbl_int:
    cmp rdi, TAG_INT
    je .Lint_identity
    cmp rdi, TAG_BOOL
    je .Lint_bool
    cmp rdi, TAG_FLOAT
    je .Lint_float
    cmp rdi, TAG_STR
    je .Lint_str
    lea rdi, [rip+.Lmsg_int_type]
    jmp rbl_fail
.Lint_identity:
    mov rdx, rsi
    mov eax, TAG_INT
    ret
.Lint_bool:
    mov rdx, rsi
    mov eax, TAG_INT
    ret
.Lint_float:
    movq xmm0, rsi
    mov rax, 0x43e0000000000000
    movq xmm1, rax
    ucomisd xmm0, xmm1
    jae .Lint_range
    mov rax, 0xc3e0000000000000
    movq xmm1, rax
    ucomisd xmm0, xmm1
    jb .Lint_range
    cvttsd2si rdx, xmm0
    mov eax, TAG_INT
    ret
.Lint_str:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rsi
    mov rdi, rsi
    lea rsi, [rbp-16]
    mov edx, 10
    call strtoll
    mov rcx, [rbp-16]
    test rcx, rcx
    jz .Lint_range
    cmp byte ptr [rcx], 0
    jne .Lint_range
    mov rdx, rax
    mov eax, TAG_INT
    leave
    ret
.Lint_range:
    leave
    lea rdi, [rip+.Lmsg_convert_range]
    jmp rbl_fail

.globl rbl_float
rbl_float:
    cmp rdi, TAG_FLOAT
    je .Lfloat_identity
    cmp rdi, TAG_INT
    je .Lfloat_int
    cmp rdi, TAG_BOOL
    je .Lfloat_bool
    cmp rdi, TAG_STR
    je .Lfloat_str
    lea rdi, [rip+.Lmsg_float_type]
    jmp rbl_fail
.Lfloat_identity:
    mov rdx, rsi
    mov eax, TAG_FLOAT
    ret
.Lfloat_int:
    cvtsi2sd xmm0, rsi
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lfloat_bool:
    cvtsi2sd xmm0, rsi
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    ret
.Lfloat_str:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rsi
    mov rdi, rsi
    lea rsi, [rbp-16]
    call strtod
    mov rcx, [rbp-16]
    test rcx, rcx
    jz .Lfloat_bad
    cmp byte ptr [rcx], 0
    jne .Lfloat_bad
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    leave
    ret
.Lfloat_bad:
    leave
    lea rdi, [rip+.Lmsg_convert_range]
    jmp rbl_fail

.globl rbl_str
rbl_str:
    cmp rdi, TAG_STR
    je .Lconv_str_identity
    cmp rdi, TAG_BOOL
    je .Lconv_str_bool
    cmp rdi, TAG_INT
    je .Lconv_str_int
    cmp rdi, TAG_FLOAT
    je .Lconv_str_float
    cmp rdi, TAG_UNIT
    je .Lconv_str_unit
    cmp rdi, TAG_LIST
    je .Lconv_str_list
    cmp rdi, TAG_DICT
    je .Lconv_str_dict
    cmp rdi, TAG_TUPLE
    je .Lconv_str_tuple
    cmp rdi, TAG_NULL
    je .Lconv_str_null
    lea rdi, [rip+.Lmsg_str_type]
    jmp rbl_fail
.Lconv_str_identity:
    mov rdx, rsi
    mov eax, TAG_STR
    ret
.Lconv_str_bool:
    test rsi, rsi
    jz .Lconv_str_false_ptr
    lea rdx, [rip+.Lstr_true]
    mov eax, TAG_STR
    ret
.Lconv_str_false_ptr:
    lea rdx, [rip+.Lstr_false]
    mov eax, TAG_STR
    ret
.Lconv_str_unit:
    lea rdx, [rip+.Lstr_unit]
    mov eax, TAG_STR
    ret
.Lconv_str_list:
    /* rbl_list_str has the same (tag, payload) -> (tag, payload) convention. */
    jmp rbl_list_str
.Lconv_str_dict:
    jmp rbl_dict_str
.Lconv_str_tuple:
    jmp rbl_tuple_str
.Lconv_str_null:
    lea rdx, [rip+.Lstr_null]
    mov eax, TAG_STR
    ret
.Lconv_str_int:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rsi
    xor edi, edi
    xor esi, esi
    lea rdx, [rip+.Lfmt_str_int]
    mov rcx, [rbp-8]
    xor eax, eax
    call snprintf
    add rax, 1
    mov [rbp-16], rax
    mov rdi, rax
    call malloc
    test rax, rax
    jz .Lconv_str_oom_int
    mov [rbp-24], rax
    mov rdi, rax
    mov rsi, [rbp-16]
    lea rdx, [rip+.Lfmt_str_int]
    mov rcx, [rbp-8]
    xor eax, eax
    call snprintf
    mov rdx, [rbp-24]
    mov eax, TAG_STR
    leave
    ret
.Lconv_str_oom_int:
    lea rdi, [rip+.Lmsg_numeric]
    call rbl_fail
    ud2
.Lconv_str_float:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rsi
    xor edi, edi
    xor esi, esi
    lea rdx, [rip+.Lfmt_str_float]
    movq xmm0, [rbp-8]
    mov eax, 1
    call snprintf
    add rax, 1
    mov [rbp-16], rax
    mov rdi, rax
    call malloc
    test rax, rax
    jz .Lconv_str_oom_float
    mov [rbp-24], rax
    mov rdi, rax
    mov rsi, [rbp-16]
    lea rdx, [rip+.Lfmt_str_float]
    movq xmm0, [rbp-8]
    mov eax, 1
    call snprintf
    mov rdx, [rbp-24]
    mov eax, TAG_STR
    leave
    ret
.Lconv_str_oom_float:
    lea rdi, [rip+.Lmsg_numeric]
    call rbl_fail
    ud2


/* ======================= RBL EXTENDED STDLIB ======================= */

.section .rodata
.Lmsg_stdarg:          .asciz "RBL runtime error: invalid standard-library arguments"
.Lmsg_string_type:     .asciz "RBL runtime error: string argument required"
.Lmsg_two_strings:     .asciz "RBL runtime error: two string arguments required"
.Lmsg_range_random:    .asciz "RBL runtime error: random_int() requires min <= max"
.Lmsg_sleep_type:      .asciz "RBL runtime error: sleep_ms() expects a non-negative int"
.Lmsg_time:            .asciz "RBL runtime error: clock_gettime failed"
.Lrand_divisor:        .double 2147483647.0

.section .bss
.align 8
.Lrand_seeded:         .quad 0

.section .text

/* Numeric conversion helper for libm wrappers. rdi=tag, rsi=payload. */
rbl_math_to_double:
    cmp rdi, TAG_FLOAT
    je .Lmtd_float
    cmp rdi, TAG_INT
    je .Lmtd_int
    lea rdi, [rip+.Lmsg_numeric]
    jmp rbl_fail
.Lmtd_float:
    movq xmm0, rsi
    ret
.Lmtd_int:
    cvtsi2sd xmm0, rsi
    ret

.globl rbl_math_pow
rbl_math_pow:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rdx
    mov [rbp-16], rcx
    call rbl_math_to_double
    movq [rbp-24], xmm0
    mov rdi, [rbp-8]
    mov rsi, [rbp-16]
    call rbl_math_to_double
    movq xmm1, xmm0
    movq xmm0, [rbp-24]
    call pow
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    leave
    ret

/* Float remainder with C fmod semantics: the result takes the sign of the
   dividend. This is the only float remainder in RBL, because '%' is integer
   only. Same (t1,p1,t2,p2) convention as rbl_math_pow. */
.globl rbl_math_fmod
rbl_math_fmod:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rdx
    mov [rbp-16], rcx
    call rbl_math_to_double
    movq [rbp-24], xmm0
    mov rdi, [rbp-8]
    mov rsi, [rbp-16]
    call rbl_math_to_double
    movq xmm1, xmm0
    movq xmm0, [rbp-24]
    call fmod
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    leave
    ret

/* hypot(a, b) = sqrt(a*a + b*b) computed without intermediate overflow. */
.globl rbl_math_hypot
rbl_math_hypot:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rdx
    mov [rbp-16], rcx
    call rbl_math_to_double
    movq [rbp-24], xmm0
    mov rdi, [rbp-8]
    mov rsi, [rbp-16]
    call rbl_math_to_double
    movq xmm1, xmm0
    movq xmm0, [rbp-24]
    call hypot
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    leave
    ret

/* sign(x) -> int: -1, 0 or 1 (NaN counts as 0). */
.globl rbl_math_sign
rbl_math_sign:
    cmp rdi, TAG_INT
    je .Lsign_int
    cmp rdi, TAG_FLOAT
    je .Lsign_float
    lea rdi, [rip+.Lmsg_numeric]
    jmp rbl_fail
.Lsign_int:
    xor edx, edx
    test rsi, rsi
    jz .Lsign_done
    mov edx, 1
    jns .Lsign_done
    mov rdx, -1
.Lsign_done:
    mov eax, TAG_INT
    ret
.Lsign_float:
    pxor xmm0, xmm0
    movq xmm1, rsi
    ucomisd xmm1, xmm0
    jp .Lsign_zero
    ja .Lsign_pos
    jb .Lsign_neg
.Lsign_zero:
    xor edx, edx
    mov eax, TAG_INT
    ret
.Lsign_pos:
    mov edx, 1
    mov eax, TAG_INT
    ret
.Lsign_neg:
    mov rdx, -1
    mov eax, TAG_INT
    ret

/* is_nan(x) -> bool: exponent all ones and a non-zero mantissa.
   is_inf(x) -> bool: exponent all ones and a zero mantissa. */
.globl rbl_math_is_nan
rbl_math_is_nan:
    cmp rdi, TAG_FLOAT
    je .Lnan_check
    cmp rdi, TAG_INT
    je .Lnan_no
    lea rdi, [rip+.Lmsg_numeric]
    jmp rbl_fail
.Lnan_check:
    mov rax, 0x7FF0000000000000
    mov rcx, rsi
    and rcx, rax
    cmp rcx, rax
    jne .Lnan_no
    mov rax, 0x000FFFFFFFFFFFFF
    test rsi, rax
    jz .Lnan_no
    mov edx, 1
    mov eax, TAG_BOOL
    ret
.Lnan_no:
    xor edx, edx
    mov eax, TAG_BOOL
    ret

.globl rbl_math_is_inf
rbl_math_is_inf:
    cmp rdi, TAG_FLOAT
    je .Linf_check
    cmp rdi, TAG_INT
    je .Linf_no
    lea rdi, [rip+.Lmsg_numeric]
    jmp rbl_fail
.Linf_check:
    mov rax, 0x7FF0000000000000
    mov rcx, rsi
    and rcx, rax
    cmp rcx, rax
    jne .Linf_no
    mov rax, 0x000FFFFFFFFFFFFF
    test rsi, rax
    jnz .Linf_no
    mov edx, 1
    mov eax, TAG_BOOL
    ret
.Linf_no:
    xor edx, edx
    mov eax, TAG_BOOL
    ret

.macro MATH_UNARY name
.globl rbl_math_\name
rbl_math_\name:
    push rbp
    mov rbp, rsp
    call rbl_math_to_double
    call \name
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    pop rbp
    ret
.endm
MATH_UNARY sin
MATH_UNARY cos
MATH_UNARY tan
MATH_UNARY log
MATH_UNARY exp
MATH_UNARY floor
MATH_UNARY ceil
MATH_UNARY round
MATH_UNARY log10
MATH_UNARY log2
MATH_UNARY trunc
.purgem MATH_UNARY

/* String -> uppercase ASCII. UTF-8 bytes >= 0x80 are copied unchanged. */
.globl rbl_string_upper
rbl_string_upper:
    cmp rdi, TAG_STR
    jne .Lstr_upper_bad
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rsi
    mov rdi, rsi
    call strlen
    mov [rbp-16], rax
    xor eax, eax
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    add rdi, 1
    call malloc
    test rax, rax
    jz .Lstr_upper_oom
    mov [rbp-32], rax
.Lstr_upper_loop:
    mov rcx, [rbp-24]
    cmp rcx, [rbp-16]
    jae .Lstr_upper_done
    mov rsi, [rbp-8]
    movzx edi, byte ptr [rsi+rcx]
    mov [rbp-24], rcx
    call toupper
    mov rcx, [rbp-24]
    mov rsi, [rbp-32]
    mov byte ptr [rsi+rcx], al
    inc rcx
    mov [rbp-24], rcx
    jmp .Lstr_upper_loop
.Lstr_upper_done:
    mov rcx, [rbp-16]
    mov rsi, [rbp-32]
    mov byte ptr [rsi+rcx], 0
    mov rdx, rsi
    mov eax, TAG_STR
    leave
    ret
.Lstr_upper_oom:
    lea rdi, [rip+.Lmsg_numeric]
    call rbl_fail
    ud2
.Lstr_upper_bad:
    lea rdi, [rip+.Lmsg_string_type]
    jmp rbl_fail

.globl rbl_string_lower
rbl_string_lower:
    cmp rdi, TAG_STR
    jne .Lstr_lower_bad
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rsi
    mov rdi, rsi
    call strlen
    mov [rbp-16], rax
    xor eax, eax
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    add rdi, 1
    call malloc
    test rax, rax
    jz .Lstr_lower_oom
    mov [rbp-32], rax
.Lstr_lower_loop:
    mov rcx, [rbp-24]
    cmp rcx, [rbp-16]
    jae .Lstr_lower_done
    mov rsi, [rbp-8]
    movzx edi, byte ptr [rsi+rcx]
    mov [rbp-24], rcx
    call tolower
    mov rcx, [rbp-24]
    mov rsi, [rbp-32]
    mov byte ptr [rsi+rcx], al
    inc rcx
    mov [rbp-24], rcx
    jmp .Lstr_lower_loop
.Lstr_lower_done:
    mov rcx, [rbp-16]
    mov rsi, [rbp-32]
    mov byte ptr [rsi+rcx], 0
    mov rdx, rsi
    mov eax, TAG_STR
    leave
    ret
.Lstr_lower_oom:
    lea rdi, [rip+.Lmsg_numeric]
    call rbl_fail
    ud2
.Lstr_lower_bad:
    lea rdi, [rip+.Lmsg_string_type]
    jmp rbl_fail

.globl rbl_string_trim
rbl_string_trim:
    cmp rdi, TAG_STR
    jne .Lstr_trim_bad
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rsi
    mov rdi, rsi
    call strlen
    mov [rbp-16], rax
    xor eax, eax
    mov [rbp-24], rax              # start
.Lstr_trim_left:
    mov rcx, [rbp-24]
    cmp rcx, [rbp-16]
    jae .Lstr_trim_all
    mov rdi, [rbp-8]
    movzx eax, byte ptr [rdi+rcx]
    cmp al, ' '
    je .Lstr_trim_left_inc
    cmp al, 9
    je .Lstr_trim_left_inc
    cmp al, 10
    je .Lstr_trim_left_inc
    cmp al, 13
    je .Lstr_trim_left_inc
    jmp .Lstr_trim_right_start
.Lstr_trim_left_inc:
    inc rcx
    mov [rbp-24], rcx
    jmp .Lstr_trim_left
.Lstr_trim_right_start:
    mov rcx, [rbp-16]
    dec rcx
    mov [rbp-32], rcx              # end
.Lstr_trim_right:
    mov rcx, [rbp-32]
    cmp rcx, [rbp-24]
    jb .Lstr_trim_copy
    mov rdi, [rbp-8]
    movzx eax, byte ptr [rdi+rcx]
    cmp al, ' '
    je .Lstr_trim_right_dec
    cmp al, 9
    je .Lstr_trim_right_dec
    cmp al, 10
    je .Lstr_trim_right_dec
    cmp al, 13
    je .Lstr_trim_right_dec
    jmp .Lstr_trim_copy
.Lstr_trim_right_dec:
    dec rcx
    mov [rbp-32], rcx
    jmp .Lstr_trim_right
.Lstr_trim_copy:
    mov rax, [rbp-32]
    sub rax, [rbp-24]
    inc rax
    mov [rbp-40], rax              # length
    lea rdi, [rax+1]
    call malloc
    test rax, rax
    jz .Lstr_trim_oom
    mov [rbp-48], rax
    mov rdi, rax
    mov rsi, [rbp-8]
    add rsi, [rbp-24]
    mov rdx, [rbp-40]
    call memcpy
    mov rax, [rbp-48]
    mov rcx, [rbp-40]
    mov byte ptr [rax+rcx], 0
    mov rdx, rax
    mov eax, TAG_STR
    leave
    ret
.Lstr_trim_all:
    mov edi, 1
    call malloc
    test rax, rax
    jz .Lstr_trim_oom
    mov byte ptr [rax], 0
    mov rdx, rax
    mov eax, TAG_STR
    leave
    ret
.Lstr_trim_oom:
    lea rdi, [rip+.Lmsg_numeric]
    call rbl_fail
    ud2
.Lstr_trim_bad:
    lea rdi, [rip+.Lmsg_string_type]
    jmp rbl_fail

.globl rbl_string_contains
rbl_string_contains:
    cmp rdi, TAG_STR
    jne .Lstr_two_bad
    cmp rdx, TAG_STR
    jne .Lstr_two_bad
    push rbp
    mov rbp, rsp
    mov rdi, rsi
    mov rsi, rcx
    call strstr
    test rax, rax
    setne al
    movzx edx, al
    mov eax, TAG_BOOL
    pop rbp
    ret

.globl rbl_string_starts_with
rbl_string_starts_with:
    cmp rdi, TAG_STR
    jne .Lstr_two_bad
    cmp rdx, TAG_STR
    jne .Lstr_two_bad
    push rbp
    mov rbp, rsp
    sub rsp, 24
    mov [rbp-8], rsi
    mov [rbp-16], rcx
    mov rdi, rsi
    call strlen
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    call strlen
    cmp rax, [rbp-24]
    ja .Lstarts_false
    mov rdi, [rbp-8]
    mov rsi, [rbp-16]
    mov rdx, rax
    call strncmp
    test eax, eax
    sete al
    movzx edx, al
    mov eax, TAG_BOOL
    leave
    ret
.Lstarts_false:
    xor edx, edx
    mov eax, TAG_BOOL
    leave
    ret

.globl rbl_string_ends_with
rbl_string_ends_with:
    cmp rdi, TAG_STR
    jne .Lstr_two_bad
    cmp rdx, TAG_STR
    jne .Lstr_two_bad
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rsi
    mov [rbp-16], rcx
    mov rdi, rsi
    call strlen
    mov [rbp-24], rax
    mov rdi, [rbp-16]
    call strlen
    mov [rbp-32], rax
    mov r8, [rbp-24]
    cmp r8, [rbp-32]
    jb .Lends_false
    sub r8, [rbp-32]
    mov rdi, [rbp-8]
    add rdi, r8
    mov rsi, [rbp-16]
    call strcmp
    test eax, eax
    sete al
    movzx edx, al
    mov eax, TAG_BOOL
    leave
    ret
.Lends_false:
    xor edx, edx
    mov eax, TAG_BOOL
    leave
    ret
.Lstr_two_bad:
    lea rdi, [rip+.Lmsg_two_strings]
    jmp rbl_fail

/* Filesystem helpers. */
.globl rbl_fs_exists
rbl_fs_exists:
    cmp rdi, TAG_STR
    jne .Lfs_bad
    push rbp
    mov rbp, rsp
    mov rdi, rsi
    xor esi, esi
    call access
    test eax, eax
    sete al
    movzx edx, al
    mov eax, TAG_BOOL
    pop rbp
    ret
.Lfs_bad:
    lea rdi, [rip+.Lmsg_string_type]
    jmp rbl_fail
.globl rbl_fs_delete
rbl_fs_delete:
    cmp rdi, TAG_STR
    jne .Lfs_bad
    push rbp
    mov rbp, rsp
    mov rdi, rsi
    call remove
    xor edx, edx
    test eax, eax
    sete dl
    mov eax, TAG_BOOL
    pop rbp
    ret
.globl rbl_fs_cwd
rbl_fs_cwd:
    push rbp
    mov rbp, rsp
    xor edi, edi
    xor esi, esi
    call getcwd
    test rax, rax
    jz .Lcwd_bad
    mov rdx, rax
    mov eax, TAG_STR
    pop rbp
    ret
.Lcwd_bad:
    lea rdi, [rip+.Lmsg_time]
    call rbl_fail
    ud2

/* Time helpers. */
.globl rbl_time_now_ms
rbl_time_now_ms:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    xor edi, edi
    lea rsi, [rbp-16]
    call clock_gettime
    test eax, eax
    jne .Ltime_bad
    mov rax, [rbp-16]
    imul rax, rax, 1000
    mov r9, rax
    mov rax, [rbp-8]
    xor edx, edx
    mov r8, 1000000
    div r8
    add rax, r9
    mov rdx, rax
    mov eax, TAG_INT
    leave
    ret
.Ltime_bad:
    lea rdi, [rip+.Lmsg_time]
    call rbl_fail
    ud2

.globl rbl_time_sleep_ms
rbl_time_sleep_ms:
    cmp rdi, TAG_INT
    jne .Lsleep_bad
    test rsi, rsi
    js .Lsleep_bad
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, rsi
    xor edx, edx
    mov rcx, 1000
    div rcx
    mov [rbp-16], rax
    mov rax, rdx
    imul rax, rax, 1000000
    mov [rbp-8], rax
    lea rdi, [rbp-16]
    xor esi, esi
    call nanosleep
    xor eax, eax
    xor edx, edx
    leave
    ret
.Lsleep_bad:
    lea rdi, [rip+.Lmsg_sleep_type]
    jmp rbl_fail

/* Random helpers. */
rbl_random_seed_once:
    cmp QWORD PTR [rip+.Lrand_seeded], 0
    jne .Lrand_seed_done
    push rbp
    mov rbp, rsp
    xor edi, edi
    call time
    mov edi, eax
    call srand
    mov QWORD PTR [rip+.Lrand_seeded], 1
    pop rbp
.Lrand_seed_done:
    ret

.globl rbl_random_int
rbl_random_int:
    cmp rdi, TAG_INT
    jne .Lrand_int_bad
    cmp rdx, TAG_INT
    jne .Lrand_int_bad
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rsi               # min
    mov [rbp-16], rcx              # max
    call rbl_random_seed_once
    mov rax, [rbp-16]
    sub rax, [rbp-8]
    jo .Lrand_int_bad_frame
    inc rax
    jz .Lrand_int_bad_frame
    mov [rbp-24], rax              # range
    mov rax, [rbp-8]
    mov [rbp-32], rax              # base
    call rand
    mov r10d, eax
    call rand
    mov r11d, eax
    shl r10, 31
    add r10, r11
    mov rax, r10
    xor edx, edx
    div QWORD PTR [rbp-24]
    add rdx, [rbp-32]
    mov rax, rdx
    mov rdx, rax
    mov eax, TAG_INT
    leave
    ret
.Lrand_int_bad_frame:
    leave
.Lrand_int_bad:
    lea rdi, [rip+.Lmsg_range_random]
    jmp rbl_fail

.globl rbl_random_float
rbl_random_float:
    push rbp
    mov rbp, rsp
    call rbl_random_seed_once
    call rand
    cvtsi2sd xmm0, eax
    movq xmm1, QWORD PTR [rip+.Lrand_divisor]
    divsd xmm0, xmm1
    movq rdx, xmm0
    mov eax, TAG_FLOAT
    pop rbp
    ret

.globl rbl_random_bool
rbl_random_bool:
    push rbp
    mov rbp, rsp
    call rbl_random_seed_once
    call rand
    and eax, 1
    mov edx, eax
    mov eax, TAG_BOOL
    pop rbp
    ret

.section .note.GNU-stack,"",@progbits
