/*
 * rbl_runtime_win.c - native Windows implementation of the RBL x86-64 runtime.
 *
 * This is the Windows counterpart of runtime/rbl_runtime.s (the GNU as /
 * System V AMD64 Linux runtime).  Generated user programs keep the exact same
 * calling convention they use on Linux:
 *
 *     arguments      : rdi, rsi, rdx, rcx, r8, r9
 *     tagged results : rax = tag, rdx = payload
 *     raw i64 helpers: rax
 *
 * so every exported entry point is declared __attribute__((sysv_abi)).
 * A two-field { uint64_t tag; uint64_t payload; } struct returned by value
 * from a sysv_abi function comes back in exactly rax:rdx, which is what the
 * generated assembly expects.
 *
 * Value model:
 *     0 = Unit, 1 = Int(i64), 2 = Float(f64 bits), 3 = String(char *), 4 = Bool
 *
 * All diagnostics go to stderr and terminate the process with exit code 1.
 * Behaviour, message strings and numeric formatting are byte-identical to the
 * Linux runtime (integers "%lld", floats "%.17g", strings "%s", print
 * arguments separated by one space, one trailing '\n', Unit prints nothing).
 *
 * Windows notes:
 *   - stdout/stderr are switched to binary mode once, and file I/O always uses
 *     "rb"/"wb", so only LF is emitted and output matches the Linux backend.
 *   - getline() is emulated with fgets() + realloc().
 *   - clock_gettime()/nanosleep() are replaced by GetTickCount64()/Sleep().
 *   - rand() (RAND_MAX == 32767 on Windows) is replaced by an internal
 *     glibc-style LCG with the same 2^31-1 range as the Linux runtime.
 *
 * C99 + Win32 only; compatible with both the UCRT and the msvcrt based
 * mingw-w64 variants, and with Windows 7+.
 */

#if defined(__MINGW32__) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <math.h>
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>

#define RBL_SYSV __attribute__((sysv_abi))

enum {
    TAG_UNIT  = 0,
    TAG_INT   = 1,
    TAG_FLOAT = 2,
    TAG_STR   = 3,
    TAG_BOOL  = 4,
    TAG_LIST  = 5,
    TAG_DICT  = 6,
    TAG_TUPLE = 7,
    TAG_NULL  = 8
};

typedef struct {
    uint64_t tag;
    uint64_t payload;
} RBLValue;

static inline RBLValue rbl_value(uint64_t tag, uint64_t payload)
{
    RBLValue v;
    v.tag = tag;
    v.payload = payload;
    return v;
}

static inline RBLValue rbl_int_value(int64_t x)
{
    return rbl_value(TAG_INT, (uint64_t)x);
}

static inline RBLValue rbl_float_value(double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    return rbl_value(TAG_FLOAT, bits);
}

static inline RBLValue rbl_bool_value(int b)
{
    return rbl_value(TAG_BOOL, b ? 1u : 0u);
}

static inline RBLValue rbl_str_value(const char *s)
{
    return rbl_value(TAG_STR, (uint64_t)(uintptr_t)s);
}

static inline RBLValue rbl_unit_value(void)
{
    return rbl_value(TAG_UNIT, 0);
}

static inline int64_t rbl_as_i64(uint64_t payload)
{
    int64_t v;
    memcpy(&v, &payload, sizeof v);
    return v;
}

static inline double rbl_as_f64(uint64_t payload)
{
    double d;
    memcpy(&d, &payload, sizeof d);
    return d;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                        */
/* ------------------------------------------------------------------ */

static void rbl_stdout_binary(void)
{
    static int done = 0;
    if (!done) {
        done = 1;
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    }
}

static void rbl_exit_now(int code)
{
    fflush(NULL);
    exit(code);
}

/* Formats a message for stderr. This helper is a normal Windows-ABI function
 * (never called from generated code), so it may be variadic. */
static void rbl_failf(const char *fmt, ...)
{
    va_list ap;
    rbl_stdout_binary();
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    rbl_exit_now(1);
}

static void rbl_oom(void)
{
    rbl_failf("RBL runtime error: %s\n", "out of memory while concatenating strings");
}

/* malloc for runtime strings: never freed (the process exit reclaims it). */
static char *rbl_alloc(size_t n)
{
    char *p = (char *)malloc(n);
    if (!p) {
        rbl_oom();
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* process / type helpers                                             */
/* ------------------------------------------------------------------ */

RBL_SYSV void rbl_process_exit(uint64_t code)
{
    rbl_stdout_binary();
    rbl_exit_now((int)(int32_t)(uint32_t)code);
}

RBL_SYSV const char *rbl_type_name(uint64_t tag)
{
    switch (tag) {
    case TAG_INT:   return "int";
    case TAG_FLOAT: return "float";
    case TAG_STR:   return "string";
    case TAG_BOOL:  return "bool";
    case TAG_LIST:  return "list";
    case TAG_DICT:  return "dict";
    case TAG_TUPLE: return "tuple";
    case TAG_NULL:  return "null";
    default:        return "()";
    }
}

RBL_SYSV void rbl_fail(const char *msg)
{
    rbl_failf("RBL runtime error: %s\n", msg);
}

/* ------------------------------------------------------------------ */
/* variable slots                                                     */
/* ------------------------------------------------------------------ */

RBL_SYSV void rbl_require_unbound(volatile uint64_t *bound, const char *name)
{
    if (*bound != 0) {
        rbl_failf("RBL runtime error: variable '%s' already declared; use let\n", name);
    }
}

RBL_SYSV RBLValue rbl_get_slot(volatile uint64_t *slot, const char *name)
{
    if (*slot == 0) {
        rbl_failf("RBL runtime error: variable '%s' not found\n", name);
    }
    return rbl_value(slot[1], slot[2]);
}

RBL_SYSV void rbl_require_rebind(volatile uint64_t *slot, uint64_t tag, const char *name)
{
    if (*slot == 0) {
        rbl_failf("RBL runtime error: variable '%s' is not declared; use set first\n", name);
    }
    if (slot[1] != tag) {
        rbl_failf("RBL runtime error: let type mismatch for '%s'\n", name);
    }
}

/* ------------------------------------------------------------------ */
/* argument / operand checks                                          */
/* ------------------------------------------------------------------ */

RBL_SYSV void rbl_check_arity(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        rbl_failf("RBL runtime error: function '%s' expects %d argument(s), got %d\n",
                  name, (int)expected, (int)actual);
    }
}

RBL_SYSV void rbl_check_type(const uint64_t *value, const char *expected,
                             const char *param, const char *func)
{
    uint64_t tag = value[0];
    const char *actual;
    int ok;

    switch (tag) {
    case TAG_INT:   actual = "int";    break;
    case TAG_FLOAT: actual = "float";  break;
    case TAG_STR:   actual = "string"; break;
    case TAG_BOOL:  actual = "bool";   break;
    default:        actual = "()";     break;
    }

    /* The generated assembly compares the parameter's declared type against
     * strcmp() of the same static string, so pointer equality holds (and is
     * what the runtime relies on); fall back to strcmp for robustness. */
    ok = (expected == actual) ||
         (expected != NULL && strcmp(expected, actual) == 0);

    if (!ok) {
        rbl_failf("RBL runtime error: parameter '%s' of '%s': expected %s, got %s\n",
                  param, func, expected, actual);
    }
}

RBL_SYSV RBLValue rbl_expect_bool(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_BOOL) {
        rbl_failf("RBL runtime error: condition must be bool, got %s\n", rbl_type_name(tag));
    }
    return rbl_bool_value(payload != 0);
}

/* The fast (unboxed) bool path in generated code reads rdx after this call,
 * so the payload is passed through unchanged. */
RBL_SYSV RBLValue rbl_expect_int(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_INT) {
        rbl_failf("RBL runtime error: range bound must be int, got %s\n", rbl_type_name(tag));
    }
    return rbl_value(TAG_INT, payload);
}

RBL_SYSV void rbl_unknown_function(const char *name)
{
    rbl_failf("RBL runtime error: function '%s' not found\n", name);
}

RBL_SYSV void rbl_unknown_method(const char *obj, const char *method)
{
    rbl_failf("RBL runtime error: unknown method '%s.%s'\n", obj, method);
}

RBL_SYSV void rbl_bad_binary(const char *op, uint64_t left_tag, uint64_t right_tag)
{
    rbl_failf("RBL runtime error: invalid operands for %s: %s and %s\n",
              op, rbl_type_name(left_tag), rbl_type_name(right_tag));
}

RBL_SYSV void rbl_bad_unary(const char *op, uint64_t tag)
{
    rbl_failf("RBL runtime error: invalid operand for %s: %s\n",
              op, rbl_type_name(tag));
}

RBL_SYSV void rbl_int_overflow(void)
{
    rbl_failf("RBL runtime error: integer overflow\n");
}

RBL_SYSV void rbl_int_divzero(void)
{
    rbl_failf("RBL runtime error: integer division by zero\n");
}

RBL_SYSV void rbl_int_div_overflow(void)
{
    rbl_failf("RBL runtime error: integer division overflow\n");
}

/* ------------------------------------------------------------------ */
/* scalar fast-path helpers (raw i64 in/out, no tagged value)         */
/* ------------------------------------------------------------------ */

RBL_SYSV int64_t rbl_inc_i64(int64_t v)
{
    if (v == INT64_MAX) {
        rbl_int_overflow();
    }
    return v + 1;
}

RBL_SYSV int64_t rbl_dec_i64(int64_t v)
{
    if (v == INT64_MIN) {
        rbl_int_overflow();
    }
    return v - 1;
}

/* ------------------------------------------------------------------ */
/* unary operators                                                    */
/* ------------------------------------------------------------------ */
/* These two do not follow the usual (rdi, rsi) convention: generated code
 * calls them with the operand still in rax (tag) and rdx (payload). */

RBL_SYSV RBLValue rbl_neg(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_INT) {
        int64_t v = rbl_as_i64(payload);
        if (v == INT64_MIN) {
            rbl_int_overflow();
        }
        return rbl_int_value(-v);
    }
    if (tag == TAG_FLOAT) {
        /* The .s version XORs the sign bit, so -0.0 -> 0.0 and NaN keeps its
         * payload except for the sign bit; negation matches that exactly. */
        return rbl_float_value(-rbl_as_f64(payload));
    }
    rbl_bad_unary("-", tag);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_not(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_BOOL) {
        rbl_bad_unary("not", tag);
    }
    return rbl_bool_value(payload == 0);
}

/* ------------------------------------------------------------------ */
/* arithmetic                                                         */
/* ------------------------------------------------------------------ */

/* Left operand: rdi = tag, rsi = payload.  Right: rdx = tag, rcx = payload. */

static inline int rbl_add_overflow(int64_t a, int64_t b, int64_t *out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, out);
#else
    *out = (int64_t)((uint64_t)a + (uint64_t)b);
    return ((a > 0) && (b > 0) && (*out < 0)) || ((a < 0) && (b < 0) && (*out >= 0));
#endif
}

static inline int rbl_sub_overflow(int64_t a, int64_t b, int64_t *out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, out);
#else
    *out = (int64_t)((uint64_t)a - (uint64_t)b);
    return ((a >= 0) && (b < 0) && (*out < 0)) || ((a < 0) && (b > 0) && (*out >= 0));
#endif
}

static inline int rbl_mul_overflow(int64_t a, int64_t b, int64_t *out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) { *out = 0; return 0; }
    if (a == -1 && b == INT64_MIN) return 1;
    if (b == -1 && a == INT64_MIN) return 1;
    *out = (int64_t)((uint64_t)a * (uint64_t)b);
    return (*out / b) != a;
#endif
}

RBL_SYSV RBLValue rbl_add(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    int64_t out;

    if (lt == TAG_INT && rt == TAG_INT) {
        if (rbl_add_overflow(rbl_as_i64(lp), rbl_as_i64(rp), &out)) {
            rbl_int_overflow();
        }
        return rbl_int_value(out);
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_float_value(rbl_as_f64(lp) + rbl_as_f64(rp));
    }
    if (lt == TAG_STR && rt == TAG_STR) {
        /* string concatenation, same allocation/overflow rules as .s */
        const char *a = (const char *)(uintptr_t)lp;
        const char *b = (const char *)(uintptr_t)rp;
        size_t la = strlen(a);
        size_t lb = strlen(b);
        char *buf;
        if (la > SIZE_MAX - lb - 1) {
            rbl_int_overflow();
        }
        buf = rbl_alloc(la + lb + 1);
        memcpy(buf, a, la);
        memcpy(buf + la, b, lb);
        buf[la + lb] = '\0';
        return rbl_str_value(buf);
    }
    rbl_bad_binary("+", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_sub(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    int64_t out;

    if (lt == TAG_INT && rt == TAG_INT) {
        if (rbl_sub_overflow(rbl_as_i64(lp), rbl_as_i64(rp), &out)) {
            rbl_int_overflow();
        }
        return rbl_int_value(out);
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_float_value(rbl_as_f64(lp) - rbl_as_f64(rp));
    }
    rbl_bad_binary("-", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_mul(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    int64_t out;

    if (lt == TAG_INT && rt == TAG_INT) {
        if (rbl_mul_overflow(rbl_as_i64(lp), rbl_as_i64(rp), &out)) {
            rbl_int_overflow();
        }
        return rbl_int_value(out);
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_float_value(rbl_as_f64(lp) * rbl_as_f64(rp));
    }
    rbl_bad_binary("*", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_div(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        int64_t a = rbl_as_i64(lp);
        int64_t b = rbl_as_i64(rp);
        if (b == 0) {
            rbl_int_divzero();
        }
        if (a == INT64_MIN && b == -1) {
            rbl_int_div_overflow();
        }
        return rbl_int_value(a / b);
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_float_value(rbl_as_f64(lp) / rbl_as_f64(rp));
    }
    rbl_bad_binary("/", lt, rt);
    return rbl_unit_value();
}

/* Integer remainder, mirroring rbl_mod in the Linux runtime: integer only, the
   same division diagnostics, and the same operand/tag conventions. */
RBL_SYSV RBLValue rbl_mod(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        int64_t a = rbl_as_i64(lp);
        int64_t b = rbl_as_i64(rp);
        if (b == 0) {
            rbl_int_divzero();
        }
        if (a == INT64_MIN && b == -1) {
            rbl_int_div_overflow();
        }
        return rbl_int_value(a % b);
    }
    rbl_bad_binary("%", lt, rt);
    return rbl_unit_value();
}

/* ------------------------------------------------------------------ */
/* comparisons and booleans                                           */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_list_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);
RBL_SYSV RBLValue rbl_dict_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);
RBL_SYSV RBLValue rbl_dict_len(uint64_t tag, uint64_t payload);
RBL_SYSV RBLValue rbl_dict_str(uint64_t tag, uint64_t payload);
RBL_SYSV RBLValue rbl_tuple_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);
RBL_SYSV RBLValue rbl_tuple_len(uint64_t tag, uint64_t payload);
RBL_SYSV RBLValue rbl_tuple_str(uint64_t tag, uint64_t payload);

RBL_SYSV RBLValue rbl_eq(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt != rt) {
        return rbl_bool_value(0);
    }
    switch (lt) {
    case TAG_INT:
    case TAG_BOOL:
        return rbl_bool_value(lp == rp);
    case TAG_LIST:
        return rbl_list_eq(lt, lp, rt, rp);
    case TAG_DICT:
        return rbl_dict_eq(lt, lp, rt, rp);
    case TAG_TUPLE:
        return rbl_tuple_eq(lt, lp, rt, rp);
    case TAG_FLOAT:
        /* ucomisd + setnp: NaN is never equal to anything */
        return rbl_bool_value(rbl_as_f64(lp) == rbl_as_f64(rp));
    case TAG_STR:
        return rbl_bool_value(strcmp((const char *)(uintptr_t)lp,
                                     (const char *)(uintptr_t)rp) == 0);
    default:
        /* Unit == Unit */
        return rbl_bool_value(1);
    }
}

RBL_SYSV RBLValue rbl_ne(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    RBLValue v = rbl_eq(lt, lp, rt, rp);
    v.payload ^= 1u;
    return v;
}

RBL_SYSV RBLValue rbl_lt(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        return rbl_bool_value(rbl_as_i64(lp) < rbl_as_i64(rp));
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        /* ucomisd + setb + setnp: unordered (NaN) compares false */
        return rbl_bool_value(rbl_as_f64(lp) < rbl_as_f64(rp));
    }
    rbl_bad_binary("<", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_gt(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        return rbl_bool_value(rbl_as_i64(lp) > rbl_as_i64(rp));
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_bool_value(rbl_as_f64(lp) > rbl_as_f64(rp));
    }
    rbl_bad_binary(">", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_le(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        return rbl_bool_value(rbl_as_i64(lp) <= rbl_as_i64(rp));
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_bool_value(rbl_as_f64(lp) <= rbl_as_f64(rp));
    }
    rbl_bad_binary("<=", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_ge(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_INT && rt == TAG_INT) {
        return rbl_bool_value(rbl_as_i64(lp) >= rbl_as_i64(rp));
    }
    if (lt == TAG_FLOAT && rt == TAG_FLOAT) {
        return rbl_bool_value(rbl_as_f64(lp) >= rbl_as_f64(rp));
    }
    rbl_bad_binary(">=", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_and(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_BOOL && rt == TAG_BOOL) {
        return rbl_bool_value((lp & rp) != 0);
    }
    rbl_bad_binary("and", lt, rt);
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_or(uint64_t lt, uint64_t lp, uint64_t rt, uint64_t rp)
{
    if (lt == TAG_BOOL && rt == TAG_BOOL) {
        return rbl_bool_value((lp | rp) != 0);
    }
    rbl_bad_binary("or", lt, rt);
    return rbl_unit_value();
}

/* Direct string concatenation helper (rdi, rsi -> rax). */
RBL_SYSV char *rbl_str_concat(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char *buf;

    if (la > SIZE_MAX - lb - 1) {
        rbl_int_overflow();
    }
    buf = rbl_alloc(la + lb + 1);
    memcpy(buf, a, la);
    memcpy(buf + la, b, lb);
    buf[la + lb] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* printing                                                           */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_list_str(uint64_t tag, uint64_t payload);

RBL_SYSV void rbl_print_one(uint64_t tag, uint64_t payload)
{
    rbl_stdout_binary();
    switch (tag) {
    case TAG_INT:
        printf("%lld", (long long)rbl_as_i64(payload));
        break;
    case TAG_FLOAT:
        printf("%.17g", rbl_as_f64(payload));
        break;
    case TAG_STR:
        printf("%s", (const char *)(uintptr_t)payload);
        break;
    case TAG_LIST: {
        RBLValue s = rbl_list_str(tag, payload);
        printf("%s", (const char *)(uintptr_t)s.payload);
        break;
    }
    case TAG_DICT: {
        RBLValue s = rbl_dict_str(tag, payload);
        printf("%s", (const char *)(uintptr_t)s.payload);
        break;
    }
    case TAG_TUPLE: {
        RBLValue s = rbl_tuple_str(tag, payload);
        printf("%s", (const char *)(uintptr_t)s.payload);
        break;
    }
    case TAG_NULL:
        fputs("null", stdout);
        break;
    case TAG_BOOL:
        fputs(payload ? "true" : "false", stdout);
        break;
    default:
        /* Unit prints as an empty string */
        break;
    }
}

RBL_SYSV void rbl_print_values(const uint64_t *values, uint64_t count)
{
    uint64_t i;
    rbl_stdout_binary();
    for (i = 0; i < count; i++) {
        rbl_print_one(values[i * 2], values[i * 2 + 1]);
        if (i + 1 < count) {
            putchar(' ');
        }
    }
    putchar('\n');
}

RBL_SYSV void rbl_log_values(const uint64_t *values, uint64_t count, uint64_t level)
{
    rbl_stdout_binary();
    fputs(level == 1 ? "[WARN] " : "[ERROR] ", stdout);
    rbl_print_values(values, count);
}

/* ------------------------------------------------------------------ */
/* stdlib: strings                                                    */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_list_len(uint64_t tag, uint64_t payload);

RBL_SYSV RBLValue rbl_len(uint64_t tag, uint64_t payload)
{
    const unsigned char *s;
    int64_t n = 0;

    if (tag == TAG_LIST) {
        return rbl_list_len(tag, payload);
    }
    if (tag == TAG_DICT) {
        return rbl_dict_len(tag, payload);
    }
    if (tag == TAG_TUPLE) {
        return rbl_tuple_len(tag, payload);
    }
    if (tag != TAG_STR) {
        rbl_fail("len() expects a string or a list");
    }
    s = (const unsigned char *)(uintptr_t)payload;
    while (*s) {
        /* count UTF-8 code points: skip continuation bytes (10xxxxxx) */
        if ((*s & 0xC0) != 0x80) {
            n++;
        }
        s++;
    }
    return rbl_int_value(n);
}

/* EOF-terminated line reader. Mirrors the Linux runtime's getline() use:
 * on success *outline is a malloc'ed NUL-terminated line (the trailing LF/CRLF
 * is stripped in place) and NULL is returned when nothing could be read, which
 * the callers report as "input() failed to read stdin". */
static char *rbl_read_line(FILE *fp, char **outline, size_t *cap, size_t *len)
{
    char *buf = *outline;
    size_t used = 0;
    size_t limit = *cap;

    if (!buf || limit == 0) {
        limit = 128;
        buf = rbl_alloc(limit);
    }
    for (;;) {
        if (used + 1 >= limit) {
            char *grown = (char *)realloc(buf, limit * 2);
            if (!grown) {
                rbl_oom();
            }
            buf = grown;
            limit *= 2;
        }
        if (!fgets(buf + used, (int)(limit - used), fp)) {
            if (used == 0) {
                *outline = buf;
                *cap = limit;
                *len = 0;
                return NULL;
            }
            break;
        }
        used += strlen(buf + used);
        if (used > 0 && buf[used - 1] == '\n') {
            break;
        }
        if (feof(fp)) {
            break;
        }
    }
    buf[used] = '\0';
    *outline = buf;
    *cap = limit;
    *len = used;
    return buf;
}

/* The Linux runtime truncates a read line at the first LF or CR (see
   rbl_trim_newline in runtime/rbl_runtime.s). input() must return the same text
   on both backends, so the same truncation happens here before the string is
   handed back to generated code. */
static char *rbl_trim_newline(char *s)
{
    char *p = s;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
        p++;
    }
    return s;
}

RBL_SYSV RBLValue rbl_input0(void)
{
    char *line = NULL;
    size_t cap = 0;
    size_t len = 0;
    char *p;

    rbl_stdout_binary();
    p = rbl_read_line(stdin, &line, &cap, &len);
    if (!p) {
        rbl_fail("input() failed to read stdin");
    }
    return rbl_str_value(rbl_trim_newline(p));
}

RBL_SYSV RBLValue rbl_input1(uint64_t tag, uint64_t payload)
{
    char *line = NULL;
    size_t cap = 0;
    size_t len = 0;
    char *p;

    if (tag != TAG_STR) {
        rbl_fail("input() prompt must be a string");
    }
    rbl_stdout_binary();
    printf("%s", (const char *)(uintptr_t)payload);
    fflush(stdout);
    p = rbl_read_line(stdin, &line, &cap, &len);
    if (!p) {
        rbl_fail("input() failed to read stdin");
    }
    return rbl_str_value(rbl_trim_newline(p));
}

/* ------------------------------------------------------------------ */
/* stdlib: files                                                      */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_read_file(uint64_t tag, uint64_t payload)
{
    FILE *fp;
    long size;
    char *buf;

    if (tag != TAG_STR) {
        rbl_fail("read_file() path must be a string");
    }
    fp = fopen((const char *)(uintptr_t)payload, "rb");
    if (!fp) {
        rbl_fail("read_file() failed to open file");
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        rbl_fail("read_file() failed while reading file");
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        rbl_fail("read_file() failed while reading file");
    }
    buf = rbl_alloc((size_t)size + 1);
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        rbl_fail("read_file() failed while reading file");
    }
    buf[size] = '\0';
    fclose(fp);
    return rbl_str_value(buf);
}

RBL_SYSV RBLValue rbl_write_file_values(const uint64_t *values, uint64_t count)
{
    const char *path;
    const char *content;
    size_t len;
    FILE *fp;

    if (count != 2 || values[0] != TAG_STR || values[2] != TAG_STR) {
        rbl_fail("write_file() expects string path and string content");
    }
    path = (const char *)(uintptr_t)values[1];
    content = (const char *)(uintptr_t)values[3];
    fp = fopen(path, "wb");
    if (!fp) {
        rbl_fail("write_file() failed to open file");
    }
    len = strlen(content);
    if (len > 0 && fwrite(content, 1, len, fp) != len) {
        fclose(fp);
        rbl_fail("write_file() failed while writing file");
    }
    fclose(fp);
    return rbl_unit_value();
}

/* ------------------------------------------------------------------ */
/* stdlib: numeric                                                    */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_abs(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_INT) {
        int64_t v = rbl_as_i64(payload);
        if (v == INT64_MIN) {
            rbl_fail("numeric conversion out of range");
        }
        return rbl_int_value(v < 0 ? -v : v);
    }
    if (tag == TAG_FLOAT) {
        return rbl_value(TAG_FLOAT, payload & 0x7fffffffffffffffull);
    }
    rbl_fail("abs() expects int or float");
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_sqrt(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_INT) {
        int64_t v = rbl_as_i64(payload);
        if (v < 0) {
            rbl_fail("numeric argument required");
        }
        return rbl_float_value(sqrt((double)v));
    }
    if (tag == TAG_FLOAT) {
        double d = rbl_as_f64(payload);
        if (d < 0.0) {
            rbl_fail("numeric argument required");
        }
        return rbl_float_value(sqrt(d));
    }
    rbl_fail("sqrt() expects int or float");
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_min_values(const uint64_t *values, uint64_t count)
{
    uint64_t i;

    if (count == 0) {
        rbl_fail("min/max require at least one argument");
    }
    if (values[0] == TAG_INT) {
        int64_t best = rbl_as_i64(values[1]);
        for (i = 1; i < count; i++) {
            int64_t v;
            if (values[i * 2] != TAG_INT) {
                rbl_fail("min/max arguments must all be int or all be float");
            }
            v = rbl_as_i64(values[i * 2 + 1]);
            if (v < best) {
                best = v;
            }
        }
        return rbl_int_value(best);
    }
    if (values[0] == TAG_FLOAT) {
        double best = rbl_as_f64(values[1]);
        for (i = 1; i < count; i++) {
            double v;
            if (values[i * 2] != TAG_FLOAT) {
                rbl_fail("min/max arguments must all be int or all be float");
            }
            v = rbl_as_f64(values[i * 2 + 1]);
            /* ucomisd xmm1, xmm0 / jae: on unordered (NaN) keep the old best */
            if (!(v >= best)) {
                best = v;
            }
        }
        return rbl_float_value(best);
    }
    rbl_fail("min/max arguments must all be int or all be float");
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_max_values(const uint64_t *values, uint64_t count)
{
    uint64_t i;

    if (count == 0) {
        rbl_fail("min/max require at least one argument");
    }
    if (values[0] == TAG_INT) {
        int64_t best = rbl_as_i64(values[1]);
        for (i = 1; i < count; i++) {
            int64_t v;
            if (values[i * 2] != TAG_INT) {
                rbl_fail("min/max arguments must all be int or all be float");
            }
            v = rbl_as_i64(values[i * 2 + 1]);
            if (v > best) {
                best = v;
            }
        }
        return rbl_int_value(best);
    }
    if (values[0] == TAG_FLOAT) {
        double best = rbl_as_f64(values[1]);
        for (i = 1; i < count; i++) {
            double v;
            if (values[i * 2] != TAG_FLOAT) {
                rbl_fail("min/max arguments must all be int or all be float");
            }
            v = rbl_as_f64(values[i * 2 + 1]);
            /* ucomisd xmm1, xmm0 / jbe: on unordered (NaN) keep the old best */
            if (!(v <= best)) {
                best = v;
            }
        }
        return rbl_float_value(best);
    }
    rbl_fail("min/max arguments must all be int or all be float");
    return rbl_unit_value();
}

/* Formats into a fresh malloc'ed buffer. fmt is always "%.17g" or "%lld". */
static char *rbl_alloc_format(const char *fmt, uint64_t payload, int is_float)
{
    char *buf;
    int need;

    if (is_float) {
        need = snprintf(NULL, 0, fmt, rbl_as_f64(payload));
    } else {
        need = snprintf(NULL, 0, fmt, (long long)rbl_as_i64(payload));
    }
    if (need < 0) {
        rbl_fail("numeric argument required");
    }
    buf = rbl_alloc((size_t)need + 1);
    if (is_float) {
        snprintf(buf, (size_t)need + 1, fmt, rbl_as_f64(payload));
    } else {
        snprintf(buf, (size_t)need + 1, fmt, (long long)rbl_as_i64(payload));
    }
    return buf;
}

RBL_SYSV RBLValue rbl_int(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_INT || tag == TAG_BOOL) {
        return rbl_int_value(rbl_as_i64(payload));
    }
    if (tag == TAG_FLOAT) {
        /* cvttsd2si semantics: reject values outside [-2^63, 2^63), NaN too */
        double d = rbl_as_f64(payload);
        if (!(d < 9223372036854775808.0) || !(d >= -9223372036854775808.0)) {
            rbl_fail("numeric conversion out of range");
        }
        return rbl_int_value((int64_t)d);
    }
    if (tag == TAG_STR) {
        const char *s = (const char *)(uintptr_t)payload;
        char *end = NULL;
        long long v;

        errno = 0;
        v = strtoll(s, &end, 10);
        if (end == s || end == NULL || *end != '\0' || errno == ERANGE) {
            rbl_fail("numeric conversion out of range");
        }
        return rbl_int_value((int64_t)v);
    }
    rbl_fail("int() cannot convert this value");
    return rbl_unit_value();
}

RBL_SYSV RBLValue rbl_float(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_FLOAT) {
        return rbl_value(TAG_FLOAT, payload);
    }
    if (tag == TAG_INT || tag == TAG_BOOL) {
        return rbl_float_value((double)rbl_as_i64(payload));
    }
    if (tag == TAG_STR) {
        const char *s = (const char *)(uintptr_t)payload;
        char *end = NULL;
        double d;

        errno = 0;
        d = strtod(s, &end);
        if (end == s || end == NULL || *end != '\0') {
            rbl_fail("numeric conversion out of range");
        }
        return rbl_float_value(d);
    }
    rbl_fail("float() cannot convert this value");
    return rbl_unit_value();
}

/* Containers live in their own translation unit, shared with the Linux build. */
RBL_SYSV RBLValue rbl_list_str(uint64_t tag, uint64_t payload);

RBL_SYSV RBLValue rbl_str(uint64_t tag, uint64_t payload)
{
    switch (tag) {
    case TAG_STR:
        return rbl_value(TAG_STR, payload);
    case TAG_BOOL:
        return rbl_str_value(payload ? "true" : "false");
    case TAG_INT:
        return rbl_str_value(rbl_alloc_format("%lld", payload, 0));
    case TAG_FLOAT:
        return rbl_str_value(rbl_alloc_format("%.17g", payload, 1));
    case TAG_UNIT:
        return rbl_str_value("()");
    case TAG_LIST:
        return rbl_list_str(tag, payload);
    case TAG_DICT:
        return rbl_dict_str(tag, payload);
    case TAG_TUPLE:
        return rbl_tuple_str(tag, payload);
    case TAG_NULL:
        return rbl_str_value("null");
    default:
        rbl_fail("str() cannot convert this value");
        return rbl_unit_value();
    }
}

/* ------------------------------------------------------------------ */
/* stdlib: math                                                       */
/* ------------------------------------------------------------------ */

static double rbl_math_to_double(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_FLOAT) {
        return rbl_as_f64(payload);
    }
    if (tag == TAG_INT) {
        return (double)rbl_as_i64(payload);
    }
    rbl_fail("numeric argument required");
    return 0.0;
}

RBL_SYSV RBLValue rbl_math_pow(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    double base = rbl_math_to_double(t1, p1);
    double expo = rbl_math_to_double(t2, p2);
    return rbl_float_value(pow(base, expo));
}

/* Float remainder, C fmod semantics (sign of the dividend) — the only float
   remainder in RBL, since '%' is integer only. */
RBL_SYSV RBLValue rbl_math_fmod(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    double a = rbl_math_to_double(t1, p1);
    double b = rbl_math_to_double(t2, p2);
    return rbl_float_value(fmod(a, b));
}

/* hypot(a, b) = sqrt(a*a + b*b) without intermediate overflow. */
RBL_SYSV RBLValue rbl_math_hypot(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    double a = rbl_math_to_double(t1, p1);
    double b = rbl_math_to_double(t2, p2);
    return rbl_float_value(hypot(a, b));
}

/* sign(x) -> int: -1, 0 or 1 (NaN counts as 0, as in the Linux runtime). */
RBL_SYSV RBLValue rbl_math_sign(uint64_t tag, uint64_t payload)
{
    double a = rbl_math_to_double(tag, payload);
    return rbl_int_value((a > 0.0) - (a < 0.0));
}

/* is_nan(x) / is_inf(x) -> bool (int arguments are finite). */
RBL_SYSV RBLValue rbl_math_is_nan(uint64_t tag, uint64_t payload)
{
    double a = rbl_math_to_double(tag, payload);
    RBLValue v;
    v.tag = TAG_BOOL;
    v.payload = (isnan(a) != 0) ? 1u : 0u;
    return v;
}

RBL_SYSV RBLValue rbl_math_is_inf(uint64_t tag, uint64_t payload)
{
    double a = rbl_math_to_double(tag, payload);
    RBLValue v;
    v.tag = TAG_BOOL;
    v.payload = (isinf(a) != 0) ? 1u : 0u;
    return v;
}

#define RBL_MATH_UNARY(name, fn)                                        \
    RBL_SYSV RBLValue name(uint64_t tag, uint64_t payload)              \
    {                                                                   \
        return rbl_float_value(fn(rbl_math_to_double(tag, payload)));   \
    }

RBL_MATH_UNARY(rbl_math_floor, floor)
RBL_MATH_UNARY(rbl_math_ceil,  ceil)
RBL_MATH_UNARY(rbl_math_round, round)
RBL_MATH_UNARY(rbl_math_log10, log10)
RBL_MATH_UNARY(rbl_math_log2,  log2)
RBL_MATH_UNARY(rbl_math_trunc, trunc)
RBL_MATH_UNARY(rbl_math_sin,   sin)
RBL_MATH_UNARY(rbl_math_cos,   cos)
RBL_MATH_UNARY(rbl_math_tan,   tan)
RBL_MATH_UNARY(rbl_math_log,   log)
RBL_MATH_UNARY(rbl_math_exp,   exp)

/* ------------------------------------------------------------------ */
/* stdlib: extended strings                                           */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_string_upper(uint64_t tag, uint64_t payload)
{
    const unsigned char *s;
    size_t n, i;
    char *buf;

    if (tag != TAG_STR) {
        rbl_fail("RBL runtime error: string argument required");
    }
    s = (const unsigned char *)(uintptr_t)payload;
    n = strlen((const char *)s);
    buf = rbl_alloc(n + 1);
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        /* ASCII-only conversion; bytes >= 0x80 are copied unchanged */
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : (char)c;
    }
    buf[n] = '\0';
    return rbl_str_value(buf);
}

RBL_SYSV RBLValue rbl_string_lower(uint64_t tag, uint64_t payload)
{
    const unsigned char *s;
    size_t n, i;
    char *buf;

    if (tag != TAG_STR) {
        rbl_fail("RBL runtime error: string argument required");
    }
    s = (const unsigned char *)(uintptr_t)payload;
    n = strlen((const char *)s);
    buf = rbl_alloc(n + 1);
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    buf[n] = '\0';
    return rbl_str_value(buf);
}

static int rbl_trim_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

RBL_SYSV RBLValue rbl_string_trim(uint64_t tag, uint64_t payload)
{
    const char *s;
    size_t n, start = 0, end, len;
    char *buf;

    if (tag != TAG_STR) {
        rbl_fail("RBL runtime error: string argument required");
    }
    s = (const char *)(uintptr_t)payload;
    n = strlen(s);

    while (start < n && rbl_trim_space((unsigned char)s[start])) {
        start++;
    }
    if (start >= n) {
        /* all whitespace -> empty string */
        buf = rbl_alloc(1);
        buf[0] = '\0';
        return rbl_str_value(buf);
    }
    end = n - 1;
    while (end > start && rbl_trim_space((unsigned char)s[end])) {
        end--;
    }
    len = end - start + 1;
    buf = rbl_alloc(len + 1);
    memcpy(buf, s + start, len);
    buf[len] = '\0';
    return rbl_str_value(buf);
}

RBL_SYSV RBLValue rbl_string_contains(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    if (t1 != TAG_STR || t2 != TAG_STR) {
        rbl_fail("RBL runtime error: two string arguments required");
    }
    return rbl_bool_value(strstr((const char *)(uintptr_t)p1,
                                 (const char *)(uintptr_t)p2) != NULL);
}

RBL_SYSV RBLValue rbl_string_starts_with(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    const char *a, *b;
    size_t la, lb;

    if (t1 != TAG_STR || t2 != TAG_STR) {
        rbl_fail("RBL runtime error: two string arguments required");
    }
    a = (const char *)(uintptr_t)p1;
    b = (const char *)(uintptr_t)p2;
    la = strlen(a);
    lb = strlen(b);
    if (lb > la) {
        return rbl_bool_value(0);
    }
    return rbl_bool_value(strncmp(a, b, lb) == 0);
}

RBL_SYSV RBLValue rbl_string_ends_with(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    const char *a, *b;
    size_t la, lb;

    if (t1 != TAG_STR || t2 != TAG_STR) {
        rbl_fail("RBL runtime error: two string arguments required");
    }
    a = (const char *)(uintptr_t)p1;
    b = (const char *)(uintptr_t)p2;
    la = strlen(a);
    lb = strlen(b);
    if (la < lb) {
        return rbl_bool_value(0);
    }
    return rbl_bool_value(strcmp(a + (la - lb), b) == 0);
}

/* ------------------------------------------------------------------ */
/* stdlib: filesystem                                                 */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_fs_exists(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_STR) {
        rbl_fail("RBL runtime error: string argument required");
    }
    return rbl_bool_value(_access((const char *)(uintptr_t)payload, 0) == 0);
}

RBL_SYSV RBLValue rbl_fs_delete(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_STR) {
        rbl_fail("RBL runtime error: string argument required");
    }
    return rbl_bool_value(_unlink((const char *)(uintptr_t)payload) == 0);
}

RBL_SYSV RBLValue rbl_fs_cwd(void)
{
    size_t cap = 256;
    char *buf;

    for (;;) {
        buf = rbl_alloc(cap);
        if (_getcwd(buf, (int)cap) != NULL) {
            return rbl_str_value(buf);
        }
        free(buf);
        if (errno != ERANGE || cap > (size_t)1 << 20) {
            rbl_fail("RBL runtime error: clock_gettime failed");
        }
        cap *= 2;
    }
}

/* ------------------------------------------------------------------ */
/* stdlib: time                                                       */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_time_now_ms(void)
{
    /* clock_gettime(CLOCK_REALTIME) in milliseconds */
    return rbl_int_value((int64_t)GetTickCount64());
}

RBL_SYSV RBLValue rbl_time_sleep_ms(uint64_t tag, uint64_t payload)
{
    int64_t ms;

    if (tag != TAG_INT) {
        rbl_fail("RBL runtime error: sleep_ms() expects a non-negative int");
    }
    ms = rbl_as_i64(payload);
    if (ms < 0) {
        rbl_fail("RBL runtime error: sleep_ms() expects a non-negative int");
    }
    Sleep((DWORD)ms);
    return rbl_unit_value();
}

/* ------------------------------------------------------------------ */
/* stdlib: random                                                     */
/* ------------------------------------------------------------------ */
/* The Linux runtime seeds libc srand() once and uses rand() with
 * RAND_MAX == 2147483647.  Windows rand() only returns 15 bits, so an
 * internal glibc-style LCG with the same 2^31-1 range is used instead. */

static uint64_t rbl_rand_state = 0;
static int rbl_rand_seeded = 0;

static void rbl_random_seed_once(void)
{
    if (!rbl_rand_seeded) {
        rbl_rand_seeded = 1;
        rbl_rand_state = (uint64_t)(uint32_t)(GetTickCount64() ^ (uint64_t)time(NULL));
    }
}

/* Returns a value in [0, 2147483647], like glibc rand(). */
static long rbl_rand(void)
{
    rbl_rand_state = rbl_rand_state * 1103515245u + 12345u;
    return (long)((rbl_rand_state >> 16) & 0x7fffffffu);
}

RBL_SYSV RBLValue rbl_random_int(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    int64_t min, max, range, base;
    uint64_t wide;

    if (t1 != TAG_INT || t2 != TAG_INT) {
        rbl_fail("RBL runtime error: random_int() requires min <= max");
    }
    min = rbl_as_i64(p1);
    max = rbl_as_i64(p2);

    rbl_random_seed_once();

    /* range = (uint64)(max - min) + 1; a wrapped/zero range is rejected, just
     * like the .s version (which also reports min > max this way). */
    range = (int64_t)((uint64_t)max - (uint64_t)min + 1u);
    if (range == 0) {
        rbl_fail("RBL runtime error: random_int() requires min <= max");
    }
    base = min;

    wide = (uint64_t)(uint32_t)rbl_rand() << 31;
    wide += (uint64_t)(uint32_t)rbl_rand();
    return rbl_int_value(base + (int64_t)(wide % (uint64_t)range));
}

RBL_SYSV RBLValue rbl_random_float(void)
{
    rbl_random_seed_once();
    return rbl_float_value((double)rbl_rand() / 2147483647.0);
}

RBL_SYSV RBLValue rbl_random_bool(void)
{
    rbl_random_seed_once();
    return rbl_bool_value((rbl_rand() & 1) != 0);
}
