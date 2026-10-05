#!/usr/bin/env python3
from __future__ import annotations
import sys, os
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import rbltool

ROOT=Path(__file__).resolve().parents[1]

CASES=[
("examples/test.rbl",0,"10\nchange number\n20\nif elif else\nbig\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n[WARN] just a warning\n5\n",""),
("tests/cases/arithmetic.rbl",0,"14\n20\n6\n7\nhello world\n",""),
("tests/cases/functions.rbl",0,"5\n55\n",""),
("tests/cases/control.rbl",0,"elif\n0\n1\n2\n2\n3\n4\n",""),
("tests/cases/bool_string_eq.rbl",0,"true\ntrue\ntrue\ntrue\nfalse\nfalse\nfalse\nfalse\ntrue\n",""),
("tests/cases/side_effect_order.rbl",0,"1\n2\n3\n",""),
("tests/cases/empty_return.rbl",0,"\n",""),
("tests/cases/scope_and_loops.rbl",0,"7\n3\n3\n",""),
("tests/cases/duplicate_functions.rbl",0,"second\n",""),
("tests/cases/unicode_ident.rbl",0,"42\n",""),
("tests/cases/float_edge.rbl",0,"1e+42\nfalse\ntrue\n",""),
("tests/cases/unit_values.rbl",0,"unit\n 1\n[WARN] \n",""),
("tests/cases/float_ops.rbl",0,"12\n8\n20\n5\n-10\ntrue\n",""),
("tests/cases/decimal_float.rbl",0,"3.5\n10\n0.25\n4\n",""),
("tests/cases/return_in_loop.rbl",0,"7\n-1\n",""),
("tests/cases/recursion.rbl",0,"55\n",""),
("tests/cases/regalloc_loop.rbl",0,"500500\n1000\n","") ,
("tests/cases/stdlib.rbl",0,"=== STDLIB ===\n6\n42\n25\n9\n4\n9\n129\n12\n123\ntrue\n321\n3.25\nRBL stdlib works\n", ""),
("tests/cases/parentheses_blocks.rbl",0,"ready\n0\n1\n2\n", ""),
("tests/cases/param_type_error.rbl",1,"","parameter 'x' of 'take': expected int, got bool"),
("tests/cases/string_concat.rbl",0,"hello RBL\ntrue\n",""),
("tests/cases/evaluation_timing.rbl",1,"","function 'takes_one' expects 1 argument(s), got 2"),
("tests/cases/error_set_twice.rbl",1,"","already declared"),
("tests/cases/error_let_unknown.rbl",1,"RHS RUNS FIRST\n","not declared"),
("tests/cases/error_short_circuit.rbl",1,"","variable 'missing_variable' not found"),
("tests/cases/error_randome.rbl",1,"", "unknown method 'rbl.randome.int'"),
("tests/cases/fast_loop_labels.rbl",0,"2\n",""),
("tests/cases/fast_loop_nested_branches.rbl",0,"9\n5\n12\n",""),
("tests/cases/selfrec_int_analysis.rbl",0,"analysis ok\n",""),
("tests/cases/loop_inc_overflow.rbl",1,"","integer overflow"),
("tests/cases/range_outside_for.rbl",1,"","range is only valid in for"),
("tests/cases/for_non_range.rbl",1,"","for (x in ...) expects a range or a list"),
("tests/cases/modulo.rbl",0,"1\n-1\n1\n-1\n0\n2\n10\n",""),
("tests/cases/modulo_by_zero.rbl",1,"","integer division by zero"),
("tests/cases/modulo_float.rbl",1,"","invalid operands for %"),
("tests/cases/literals.rbl",0,"255\n255\n10\n493\n17\n1000000\n65535\n1000\n0.0015\n200\n1.5\n10\n1000\n10.5\n10000000000\n",""),
("tests/cases/comment_hash.rbl",0,"before\n1\n",""),
("tests/cases/strings_escapes.rbl",0,"tab[\t]nl\nquote[\"]slash[\\]\nhex[A]u[\u2713]U[\U0001F600]\nline one\nline two\n6\nab\n",""),
("tests/cases/fstrings.rbl",0,"hello, Reiwa!\n42 + 1 = 43\nlen=5\nbraces {} and 42\nfloat 1.5 bool true\ntab\t42\nnext\nReiwaReiwa\n0\nno parts here\n",""),
("tests/cases/compound_assign.rbl",0,"2\nab\n3\n",""),
("tests/cases/break_continue.rbl",0,"12\n2\n0 0\n1 0\n2 0\n",""),
("tests/cases/math_ext.rbl",0,"3\n3\n3\n-3\n2\n10\n9\n2\n",""),
("tests/cases/math_fmod.rbl",0,"1.5\n-1.5\n1.5\n1\n0\n",""),
("tests/cases/math_angles.rbl",0,"true\ntrue\n0\n0\n",""),
("tests/cases/math_hypot.rbl",0,"5\n5\n0\ntrue\n",""),
("tests/cases/math_constants.rbl",0,"true\ntrue\ntrue\ntrue\n",""),
("tests/cases/math_clamp.rbl",0,"5\n1\n10\n2\n",""),
("tests/cases/math_sign.rbl",0,"1\n-1\n0\n1\n-1\n0\n",""),
("tests/cases/math_nan_inf.rbl",0,"true\ntrue\nfalse\nfalse\nfalse\nfalse\n",""),
("tests/cases/for_condition.rbl",0,"0\n1\n2\nafter\n4\n1\n3\n5\n7\n9\n",""),
("tests/cases/ternary.rbl",0,"20\n10\nyes\n11\n1\n10\n",""),
("tests/cases/switch.rbl",0,"two-or-three\nfour-to-nine\nstring case\nother\n",""),
("tests/cases/lists.rbl",0,"[1, 2, 3]\n3\n1\n3\n[1, 20, 3]\n[99, two, 3.5, true]\n4\n[7, 70]\n24\n[[1, 2], [3]]\n2\n1\ntrue\nfalse\nfalse\n[0, 10, 20, 30]\n",""),
("tests/cases/dicts.rbl",0,"{b: 2, a: 1}\n2\n1\n2\n\n3\n3\n0\n{2.5: half, 1: one}\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\n100\n3\ntrue\nfalse\n",""),
("tests/cases/dict_param_type.rbl",0,"3\ntrue\n",""),
("tests/cases/tuples_structs.rbl",0,"(1, 2, 3)\n3\n1\n3\n(1, (2, 3))\n3\ntrue\nfalse\n0\n1\n2\n3\n{x: 3, y: 4}\n3\n4\n30\n2\ntrue\ntrue\nfalse\n35\n10\n{x: 35, y: 10}\n",""),
("tests/cases/game_of_life.rbl",0,"PASS: generation 1\nPASS: generation 2 returns to start\n",""),
("tests/cases/gc_pressure.rbl",0,"50000\n",""),
("tests/cases/null_value.rbl",0,"null\ntrue\nfalse\ntrue\ntrue\nvalue\nnull\n\nnull\n2\nfalse\n[null, 1]\ntrue\n",""),
]

def main():
    failures=[]
    print('RBL direct ASM regression suite')
    for rel, expected_rc, expected_out, stderr_need in CASES:
        src=ROOT/rel
        rc,out,err=rbltool.run_program(src,{})
        ok=(rc==expected_rc and out==expected_out and (not stderr_need or all(x in err for x in stderr_need.split(' and '))))
        status='PASS' if ok else 'FAIL'
        print(f'[{status}] {rel}')
        if not ok:
            failures.append((rel,rc,out,err))
    if failures:
        print('\nFailures:')
        for rel,rc,out,err in failures:
            print('\n---',rel,'rc=',rc,'---\nSTDOUT:\n',out,'STDERR:\n',err)
        return 1
    print(f'\nALL {len(CASES)} RBL DIRECT ASM TESTS PASSED')
    return 0
if __name__=='__main__': raise SystemExit(main())
