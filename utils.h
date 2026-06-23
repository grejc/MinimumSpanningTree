/// @file utils.h
/// @brief Lib of utils written in C with love and hate by @grejc
///
/// Features:
/// - Terminal colors
/// - Styled print_error & print_success macros
/// - Styled test macros (based on assert.h but with steroids)
/// - Box utils
/// - Table utils
/// - Terminal menu (with keyboard interaction)
/// - Progress Bars
/// - ArgParser (Python Style) with `--help` automatic
///   - init
///   - add_flag
///   - add_option
///   - add_pos

#ifndef GREJC_UTILS_H
#define GREJC_UTILS_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// ============================================================================
// TERMINAL UTILS
// ============================================================================

/// @brief Clears the entire terminal screen.
///
/// Sends the ANSI escape sequence `\033[H\033[J` which moves the cursor to
/// the home position (1,1) and clears the screen from that point onward.
#define TERMINAL_CLEAN_SCREEN() printf("\033[H\033[J")

/// @brief Moves the cursor to an absolute position on the terminal.
///
/// @param row Row number (1-indexed).
/// @param col Column number (1-indexed).
#define TERMINAL_MOVE_CURSOR(row, col) printf("\033[%d;%dH", row, col)

/// @brief Moves the cursor up by `n` rows.
///
/// @param n Number of rows to move up.
#define TERMINAL_MOVE_CURSOR_UP(n) printf("\033[%dA", n)

/// @brief Moves the cursor down by `n` rows.
///
/// @param n Number of rows to move down.
#define TERMINAL_MOVE_CURSOR_DOWN(n) printf("\033[%dB", n)

/// @brief Moves the cursor left by `n` columns.
///
/// @param n Number of columns to move left.
#define TERMINAL_MOVE_CURSOR_LEFT(n) printf("\033[%dD", n)

/// @brief Moves the cursor right by `n` columns.
///
/// @param n Number of columns to move right.
#define TERMINAL_MOVE_CURSOR_RIGHT(n) printf("\033[%dC", n)

/// @brief Moves the cursor to the home position (1, 1).
#define TERMINAL_MOVE_CURSOR_HOME() printf("\033[H")

/// @brief Moves the cursor to the bottom row of the terminal.
#define TERMINAL_MOVE_CURSOR_END() printf("\033[F")

/// @defgroup terminal_colors Terminal Color Codes
///
/// ANSI escape sequences for changing terminal text color.
/// Use these constants with printf or fprintf to colorize output.
/// Always reset with @ref TERMINAL_COLOR_RESET after the colored text.
///
/// @{
/// @name Color Constants
/// @{
#define TERMINAL_COLOR_RESET "\033[0m"
#define TERMINAL_COLOR_BLACK "\033[30m"
#define TERMINAL_COLOR_RED "\033[31m"
#define TERMINAL_COLOR_GREEN "\033[32m"
#define TERMINAL_COLOR_YELLOW "\033[33m"
#define TERMINAL_COLOR_BLUE "\033[34m"
#define TERMINAL_COLOR_MAGENTA "\033[35m"
#define TERMINAL_COLOR_CYAN "\033[36m"
#define TERMINAL_COLOR_WHITE "\033[37m"
/// @}
/// @}

/// @brief Stores the dimensions of the terminal window.
typedef struct term_dim_s {
  unsigned width;  ///< Width in columns.
  unsigned height; ///< Height in rows.
} term_dim_t;

/// @brief Retrieves the current terminal dimensions via an ioctl call.
///
/// Uses `TIOCGWINSZ` on `STDOUT_FILENO` to obtain the terminal size.
/// Falls back to 80x24 if the ioctl fails or returns zero.
///
/// @return A @ref term_dim_t struct with the current terminal width and height.
static inline term_dim_t _get_terminal_dimensions() {
  struct winsize w;
  term_dim_t td;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    td.width = w.ws_col > 0 ? w.ws_col : 80;
    td.height = w.ws_row > 0 ? w.ws_row : 24;
  } else {
    td.width = 80;
    td.height = 24;
  }

  return td;
}

/// @brief Prints a formatted error message to an output stream, with an
///        optional program exit.
///
/// If @p output_stream is NULL, the message is printed to stdout with ANSI
/// color codes for visual emphasis. If a valid FILE pointer is provided, the
/// message is printed without colors (suitable for file logging).
///
/// When @p exit_code is not -1, the program terminates with that exit code
/// immediately after printing.
///
/// @param error_reason A short string identifying the reason for the error
///                     (e.g. "File not found").
/// @param message      A detailed error description.
/// @param output_stream FILE pointer to redirect output, or NULL for colored
///                      stdout output.
/// @param exit_code    Exit code passed to `exit()`. If -1, the program does
///                     **not** exit.
///
/// Example:
/// @code{.c}
/// print_error("Error reason", "Error message", NULL, 1);
/// @endcode
///
/// Output (colored when output_stream is NULL):
/// @code
/// [ERROR]{LINE -> 45}: Error reason
///   ->  Error message
/// @endcode
#define print_error(error_reason, message, output_stream, exit_code)           \
  do {                                                                         \
    FILE *_pe_stream = (output_stream);                                        \
    if (_pe_stream == NULL)                                                    \
      fprintf(stdout, "[%sERROR%s]{%sLINE \u2192 %d%s}:\t%s%s\n\t%s%s%s\n",    \
              TERMINAL_COLOR_RED, TERMINAL_COLOR_RESET, TERMINAL_COLOR_BLUE,   \
              __LINE__, TERMINAL_COLOR_RESET, TERMINAL_COLOR_RED,              \
              (error_reason), TERMINAL_COLOR_YELLOW, (message),                \
              TERMINAL_COLOR_RESET);                                           \
    else                                                                       \
      fprintf(_pe_stream, "[ERROR]{LINE \u2192 %d}: %s\n\t%s\n", __LINE__,     \
              (error_reason), (message));                                      \
    if ((exit_code) != -1)                                                     \
      exit(exit_code);                                                         \
  } while (0)

/// @brief Prints a success message to an output stream.
///
/// If @p output_stream is NULL, the message is printed to stdout with a green
/// color. If a valid FILE pointer is provided, the message is printed without
/// colors.
///
/// @param output_stream FILE pointer to redirect output, or NULL for colored
///                      stdout output.
/// @param message       The success message to display (printf-style format).
/// @param ...           Optional variadic arguments for @p message.
///
/// @par Overloads (based on argument count):
/// | Args | Signature                               | Behaviour           |
/// |------|-----------------------------------------|---------------------|
/// | 2    | `print_success(output_stream, message)` | Literal message.    |
/// | 3+   | `print_success(output_stream, fmt, ...)`| printf-style msg.   |
///
/// Example:
/// @code{.c}
/// print_success(NULL, "Operation completed");
/// print_success(NULL, "Processed %d items in %.2fs", count, elapsed);
/// @endcode
///
/// Output (colored when output_stream is NULL):
/// @code
/// [SUCCESS]{LINE -> 45}:
///   Operation completed
/// @endcode
#define PRINT_SUCCESS_VA_NUM_ARGS(...)                                         \
  PRINT_SUCCESS_VA_NUM_ARGS_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define PRINT_SUCCESS_VA_NUM_ARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

#define PRINT_SUCCESS_DISPATCH(N) PRINT_SUCCESS_DISPATCH_IMPL(N)
#define PRINT_SUCCESS_DISPATCH_IMPL(N) print_success_##N

#define print_success_2(output_stream, message)                                \
  do {                                                                         \
    FILE *_ps_stream = (output_stream);                                        \
    if (_ps_stream == NULL)                                                    \
      fprintf(stdout, "[%sSUCCESS%s]{%sLINE \u2192 %d%s}:\n\t%s%s%s\n",        \
              TERMINAL_COLOR_GREEN, TERMINAL_COLOR_RESET, TERMINAL_COLOR_BLUE, \
              __LINE__, TERMINAL_COLOR_RESET, TERMINAL_COLOR_GREEN, message,   \
              TERMINAL_COLOR_RESET);                                           \
    else                                                                       \
      fprintf(_ps_stream, "[SUCCESS]{LINE \u2192 %d}:\n\t%s\n", __LINE__,      \
              message);                                                        \
  } while (0)

#define print_success_3(output_stream, fmt, ...)                               \
  do {                                                                         \
    FILE *_ps_stream = (output_stream);                                        \
    if (_ps_stream == NULL) {                                                  \
      fprintf(stdout, "[%sSUCCESS%s]{%sLINE \u2192 %d%s}:\n\t%s",              \
              TERMINAL_COLOR_GREEN, TERMINAL_COLOR_RESET, TERMINAL_COLOR_BLUE, \
              __LINE__, TERMINAL_COLOR_RESET, TERMINAL_COLOR_GREEN);           \
      fprintf(stdout, fmt, __VA_ARGS__);                                       \
      fprintf(stdout, "%s\n", TERMINAL_COLOR_RESET);                           \
    } else {                                                                   \
      fprintf(_ps_stream, "[SUCCESS]{LINE \u2192 %d}:\n\t", __LINE__);         \
      fprintf(_ps_stream, fmt, __VA_ARGS__);                                   \
      fprintf(_ps_stream, "\n");                                               \
    }                                                                          \
  } while (0)

#define print_success_4(output_stream, fmt, ...)                               \
  print_success_3(output_stream, fmt, __VA_ARGS__)
#define print_success_5(output_stream, fmt, ...)                               \
  print_success_3(output_stream, fmt, __VA_ARGS__)
#define print_success_6(output_stream, fmt, ...)                               \
  print_success_3(output_stream, fmt, __VA_ARGS__)

#define print_success(...)                                                     \
  PRINT_SUCCESS_DISPATCH(PRINT_SUCCESS_VA_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)

// ============================================================================
// TEST UTILS (Based on assert.h - Enhanced Version)
// ============================================================================

#ifdef NDEBUG
#define test(...) ((void)0)
#define test_op(...) ((void)0)
#else

/// @brief Resolves the current function name in a portable way.
///
/// Uses `__func__` (C99+), `__PRETTY_FUNCTION__` (GCC), or `"unknown"` as a
/// fallback.  The result is embedded in the failure report of @ref test and
/// @ref test_op. @see GREJC_FAIL_REPORT_BASE, @see GREJC_FAIL_REPORT_OP_BASE
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
#define GREJC_TEST_FUNCTION __func__
#elif defined __GNUC__
#define GREJC_TEST_FUNCTION __extension__ __PRETTY_FUNCTION__
#else
#define GREJC_TEST_FUNCTION "unknown"
#endif

/// @name Type-Specific Value Printers
///
/// Static helper functions invoked by @ref GREJC_PRINT_VAL to format a value
/// of a known type to stderr.  Each handles one C type and is selected at
/// compile time via `_Generic` dispatch.
///
/// @{
static inline void _grejc_p_bool(_Bool x) {
  fprintf(stderr, "%s", x ? "true" : "false");
}
static inline void _grejc_p_char(char x) {
  fprintf(stderr, "'%c' (0x%02X)", x, x);
}
static inline void _grejc_p_schar(signed char x) { fprintf(stderr, "%d", x); }
static inline void _grejc_p_uchar(unsigned char x) { fprintf(stderr, "%u", x); }
static inline void _grejc_p_short(short x) { fprintf(stderr, "%d", x); }
static inline void _grejc_p_ushort(unsigned short x) {
  fprintf(stderr, "%u", x);
}
static inline void _grejc_p_int(int x) { fprintf(stderr, "%d", x); }
static inline void _grejc_p_uint(unsigned int x) { fprintf(stderr, "%u", x); }
static inline void _grejc_p_long(long x) { fprintf(stderr, "%ld", x); }
static inline void _grejc_p_ulong(unsigned long x) {
  fprintf(stderr, "%lu", x);
}
static inline void _grejc_p_llong(long long x) { fprintf(stderr, "%lld", x); }
static inline void _grejc_p_ullong(unsigned long long x) {
  fprintf(stderr, "%llu", x);
}
static inline void _grejc_p_float(float x) { fprintf(stderr, "%f", x); }
static inline void _grejc_p_double(double x) { fprintf(stderr, "%f", x); }
static inline void _grejc_p_str(char *x) {
  fprintf(stderr, "\"%s\"", x ? x : "NULL");
}
static inline void _grejc_p_cstr(const char *x) {
  fprintf(stderr, "\"%s\"", x ? x : "NULL");
}
static inline void _grejc_p_ptr(const void *x) { fprintf(stderr, "%p", x); }
/// @}

/// @brief Resolves any expression to a human-readable C type name at compile
///        time via `_Generic`.
///
/// The returned string is used in the failure report of @ref test (the "Result
/// Type" line).  Pointers and unrecognised struct types fall through to the
/// `default` label.
///
/// @param x  Any expression whose type is to be named.
/// @return A string literal describing the type of @p x.
#define GREJC_TYPE_STR(x)                                                      \
  _Generic((x),                                                                \
      _Bool: "bool",                                                           \
      char: "char",                                                            \
      signed char: "signed char",                                              \
      unsigned char: "unsigned char",                                          \
      short: "short",                                                          \
      unsigned short: "unsigned short",                                        \
      int: "int",                                                              \
      unsigned int: "unsigned int",                                            \
      long: "long",                                                            \
      unsigned long: "unsigned long",                                          \
      long long: "long long",                                                  \
      unsigned long long: "unsigned long long",                                \
      float: "float",                                                          \
      double: "double",                                                        \
      char *: "string (char*)",                                                \
      const char *: "string (const char*)",                                    \
      default: "pointer / structure reference")

/// @brief Prints a typed value to stderr in a human-readable format.
///
/// Uses `_Generic` to select the correct type-specific printer function
/// (from the @ref _grejc_p_* family) at compile time, then immediately
/// invokes it with @p x.
///
/// Booleans are printed as `true`/`false`, characters as `'c' (0x63)`,
/// strings as `"..."` (or `NULL`), and pointers with `%p`.  All other types
/// use their natural decimal or floating-point format.
///
/// @param x  The value to print.
#define GREJC_PRINT_VAL(x)                                                     \
  _Generic((x),                                                                \
      _Bool: _grejc_p_bool,                                                    \
      char: _grejc_p_char,                                                     \
      signed char: _grejc_p_schar,                                             \
      unsigned char: _grejc_p_uchar,                                           \
      short: _grejc_p_short,                                                   \
      unsigned short: _grejc_p_ushort,                                         \
      int: _grejc_p_int,                                                       \
      unsigned int: _grejc_p_uint,                                             \
      long: _grejc_p_long,                                                     \
      unsigned long: _grejc_p_ulong,                                           \
      long long: _grejc_p_llong,                                               \
      unsigned long long: _grejc_p_ullong,                                     \
      float: _grejc_p_float,                                                   \
      double: _grejc_p_double,                                                 \
      char *: _grejc_p_str,                                                    \
      const char *: _grejc_p_cstr,                                             \
      default: _grejc_p_ptr)(x)

/// @brief Prints the standard failure-report header for @ref test (without
///        exit or user message).
///
/// Emits the coloured diagnostic block (file, line, expression text, resolved
/// type, evaluated value, enclosing function) to stderr.  The caller is
/// responsible for printing any optional message and calling `exit(1)`.
///
/// @param expr_str   Stringified expression text.
/// @param type_str   Human-readable type name (from @ref GREJC_TYPE_STR).
/// @param print_stmt Single statement (semicolon-terminated) that prints the
///                   evaluated value to stderr (typically @ref
///                   GREJC_PRINT_VAL).
#define GREJC_FAIL_REPORT_BASE(expr_str, type_str, print_stmt)                 \
  do {                                                                         \
    fprintf(stderr, "[%sTEST FAILED%s]{%s%s:%d%s}:\n", TERMINAL_COLOR_RED,     \
            TERMINAL_COLOR_RESET, TERMINAL_COLOR_BLUE, __FILE__, __LINE__,     \
            TERMINAL_COLOR_RESET);                                             \
    fprintf(stderr, "\t%sExpression:\t%s%s%s\n", TERMINAL_COLOR_YELLOW,        \
            TERMINAL_COLOR_RED, expr_str, TERMINAL_COLOR_RESET);               \
    fprintf(stderr, "\t%sResult Type:\t%s%s%s\n", TERMINAL_COLOR_YELLOW,       \
            TERMINAL_COLOR_CYAN, type_str, TERMINAL_COLOR_RESET);              \
    fprintf(stderr, "\t%sEvaluated:\t%s", TERMINAL_COLOR_YELLOW,               \
            TERMINAL_COLOR_RED);                                               \
    print_stmt;                                                                \
    fprintf(stderr, "%s\n", TERMINAL_COLOR_RESET);                             \
    fprintf(stderr, "\t%sIn function:\t%s%s%s\n", TERMINAL_COLOR_YELLOW,       \
            TERMINAL_COLOR_MAGENTA, GREJC_TEST_FUNCTION,                       \
            TERMINAL_COLOR_RESET);                                             \
  } while (0)

/// @brief Counts the number of variadic macro arguments (up to 8).
///
/// Internal helper used by the overload-dispatch mechanism of @ref test and
/// @ref test_op.
///
/// @param ...  Variadic arguments (1 to 8).
/// @return     The argument count as an integer preprocessor token.
#define GREJC_VA_NUM_ARGS(...)                                                 \
  GREJC_VA_NUM_ARGS_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define GREJC_VA_NUM_ARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

/// @brief 1-argument overload: assert @p expr with no message. @see test
#define test_1(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      GREJC_FAIL_REPORT_BASE(#expr, GREJC_TYPE_STR(expr),                      \
                             GREJC_PRINT_VAL(expr));                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief 2-argument overload: assert @p expr with a literal format string
///        (no variadic args). @see test
#define test_2(expr, fmt)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      GREJC_FAIL_REPORT_BASE(#expr, GREJC_TYPE_STR(expr),                      \
                             GREJC_PRINT_VAL(expr));                           \
      fprintf(stderr, "\t%sMessage:\t" TERMINAL_COLOR_RESET fmt "\n",          \
              TERMINAL_COLOR_YELLOW);                                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief 3+ argument overload: assert @p expr with a printf-style message.
///        `test_4`/`test_5`/`test_6` delegate here. @see test
#define test_3(expr, fmt, ...)                                                 \
  do {                                                                         \
    if (!(expr)) {                                                             \
      GREJC_FAIL_REPORT_BASE(#expr, GREJC_TYPE_STR(expr),                      \
                             GREJC_PRINT_VAL(expr));                           \
      fprintf(stderr, "\t%sMessage:\t" TERMINAL_COLOR_RESET fmt "\n",          \
              TERMINAL_COLOR_YELLOW, __VA_ARGS__);                             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief Alias for @ref test_3 (4-argument form). @see test
#define test_4(expr, fmt, ...) test_3(expr, fmt, __VA_ARGS__)
/// @brief Alias for @ref test_3 (5-argument form). @see test
#define test_5(expr, fmt, ...) test_3(expr, fmt, __VA_ARGS__)
/// @brief Alias for @ref test_3 (6-argument form). @see test
#define test_6(expr, fmt, ...) test_3(expr, fmt, __VA_ARGS__)

/// @brief Token-pastes `GREJC_VA_NUM_ARGS` onto `test_` to select the right
///        overload (e.g. `test_1`, `test_2`, …).
#define GREJC_DISPATCH(N) GREJC_DISPATCH_IMPL(N)
#define GREJC_DISPATCH_IMPL(N) test_##N

/// @brief Assert that an expression evaluates to true (non-zero).
///
/// A drop-in enhancement over standard `assert()` that provides colourful,
/// detailed failure diagnostics on stderr. When `NDEBUG` is defined the macro
/// expands to `((void)0)` and produces no code, matching `assert.h` semantics.
///
/// On failure the report includes:
/// - Source file and line number.
/// - The expression text as written.
/// - The expression's resolved type (via `_Generic` introspection —
///   see @ref GREJC_TYPE_STR).
/// - The expression's actual evaluated value (formatted appropriately for its
///   type via @ref GREJC_PRINT_VAL).
/// - The enclosing function name.
/// - An optional user-supplied message (printf-style).
///
/// The program then terminates with `exit(1)`.
///
/// @par Overloads (based on argument count):
/// | Args | Signature                        | Behaviour |
/// |------|----------------------------------|-------------------------------------|
/// | 1    | `test(expr)`                     | Assert @p expr, no message. | |
/// 2    | `test(expr, fmt)`                | Assert @p expr, literal `fmt` (no
/// variadic args). | | 3+   | `test(expr, fmt, ...)`           | Assert @p
/// expr, printf-style message with args.   |
///
/// @param expr  Boolean expression to assert (must be true).
/// @param fmt   Optional `printf`-style format string (included in the failure
///              report only on assertion failure).
/// @param ...   Optional variadic arguments for @p fmt.
///
/// @par Example — basic assertion:
/// @code{.c}
/// int x = 42;
/// test(x == 42);
/// @endcode
///
/// @par Example — with custom message:
/// @code{.c}
/// int result = compute();
/// test(result >= 0, "Expected non-negative result, got %d", result);
/// @endcode
///
/// @par Example — disabled via NDEBUG:
/// @code{.c}
/// #define NDEBUG
/// #include "utils.h"
/// test(0); // expands to ((void)0) — no-op
/// @endcode
///
/// @note The @ref GREJC_PRINT_VAL macro handles `char*` and `const char*`
///       specially (printing them as quoted strings) and pointers via `%p`.
///
/// @see test_op For asserting binary-relation expressions with explicit
///              left-hand-side, operator, and right-hand-side breakdown.
#define test(...) GREJC_DISPATCH(GREJC_VA_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)

/// @brief Prints the standard failure-report header for @ref test_op (without
///        exit or user message).
///
/// Emits the coloured diagnostic block (file, line, full expression text,
/// "Anatomy" line with run-time values of both operands, enclosing function)
/// to stderr.  The caller is responsible for printing any optional message
/// and calling `exit(1)`.
///
/// @param lhs_str  Stringified left-hand operand text.
/// @param op_str   Stringified operator token.
/// @param rhs_str  Stringified right-hand operand text.
/// @param lhs_val  Evaluated left-hand operand (any type).
/// @param rhs_val  Evaluated right-hand operand (any type).
#define GREJC_FAIL_REPORT_OP_BASE(lhs_str, op_str, rhs_str, lhs_val, rhs_val)  \
  do {                                                                         \
    fprintf(stderr, "[%sTEST FAILED%s]{%s%s:%d%s}:\n", TERMINAL_COLOR_RED,     \
            TERMINAL_COLOR_RESET, TERMINAL_COLOR_BLUE, __FILE__, __LINE__,     \
            TERMINAL_COLOR_RESET);                                             \
    fprintf(stderr, "\t%sExpression:\t%s%s %s %s%s\n", TERMINAL_COLOR_YELLOW,  \
            TERMINAL_COLOR_RED, lhs_str, op_str, rhs_str,                      \
            TERMINAL_COLOR_RESET);                                             \
    fprintf(stderr, "\t%sAnatomy:\t%s", TERMINAL_COLOR_YELLOW,                 \
            TERMINAL_COLOR_RESET);                                             \
    GREJC_PRINT_VAL(lhs_val);                                                  \
    fprintf(stderr, " %s ", op_str);                                           \
    GREJC_PRINT_VAL(rhs_val);                                                  \
    fprintf(stderr, "\n\t%sIn function:\t%s%s%s\n", TERMINAL_COLOR_YELLOW,     \
            TERMINAL_COLOR_MAGENTA, GREJC_TEST_FUNCTION,                       \
            TERMINAL_COLOR_RESET);                                             \
  } while (0)

/// @brief 3-argument overload: assert `lhs op rhs` with no message. @see
/// test_op
#define test_op_3(lhs, op, rhs)                                                \
  do {                                                                         \
    if (!((lhs)op(rhs))) {                                                     \
      GREJC_FAIL_REPORT_OP_BASE(#lhs, #op, #rhs, lhs, rhs);                    \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief 4-argument overload: assert `lhs op rhs` with a literal format
///        string (no variadic args). @see test_op
#define test_op_4(lhs, op, rhs, fmt)                                           \
  do {                                                                         \
    if (!((lhs)op(rhs))) {                                                     \
      GREJC_FAIL_REPORT_OP_BASE(#lhs, #op, #rhs, lhs, rhs);                    \
      fprintf(stderr, "\t%sMessage:\t" TERMINAL_COLOR_RESET fmt "\n",          \
              TERMINAL_COLOR_YELLOW);                                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief 5+ argument overload: assert `lhs op rhs` with a printf-style
///        message. @see test_op
#define test_op_5(lhs, op, rhs, fmt, ...)                                      \
  do {                                                                         \
    if (!((lhs)op(rhs))) {                                                     \
      GREJC_FAIL_REPORT_OP_BASE(#lhs, #op, #rhs, lhs, rhs);                    \
      fprintf(stderr, "\t%sMessage:\t" TERMINAL_COLOR_RESET fmt "\n",          \
              TERMINAL_COLOR_YELLOW, __VA_ARGS__);                             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/// @brief Token-pastes `GREJC_VA_NUM_ARGS` onto `test_op_` to select the
///        right overload (e.g. `test_op_3`, `test_op_4`, …).
///
/// Separate from @ref GREJC_DISPATCH because `test_op`'s overloads start at
/// 3 arguments rather than 1.
#define GREJC_DISPATCH_OP(N) GREJC_DISPATCH_OP_IMPL(N)
#define GREJC_DISPATCH_OP_IMPL(N) test_op_##N

/// @brief Assert that a binary relational/equality operation holds true.
///
/// Like @ref test but specialised for binary-operator expressions such as
/// `a == b`, `x > 0`, or `strcmp(s1, s2) == 0`. The macro takes the left-hand
/// side, the operator, and the right-hand side as three separate arguments so
/// that the failure report can show both the **source text** and the
/// **evaluated values** of each operand individually ("Anatomy" line).
///
/// When `NDEBUG` is defined the macro expands to `((void)0)`.
///
/// On failure the report includes:
/// - Source file and line number.
/// - The full expression text (`lhs op rhs`).
/// - An "Anatomy" line showing the actual run-time values of @p lhs and @p rhs
///   separated by the operator (formatted via @ref GREJC_PRINT_VAL).
/// - The enclosing function name.
/// - An optional user-supplied message (printf-style).
///
/// The program then terminates with `exit(1)`.
///
/// @par Overloads (based on argument count):
/// | Args | Signature                            | Behaviour |
/// |------|--------------------------------------|------------------------------------------|
/// | 3    | `test_op(lhs, op, rhs)`              | Assert `lhs op rhs`, no
/// message.         | | 4    | `test_op(lhs, op, rhs, fmt)`         | Assert
/// with literal `fmt` (no variadic). | | 5+   | `test_op(lhs, op, rhs, fmt,
/// ...)`    | Assert with printf-style message.        |
///
/// @param lhs Left-hand operand of the binary expression.
/// @param op  Relational or equality operator (`==`, `!=`, `<`, `>`, `<=`,
///            `>=` — passed as a token, not a string).
/// @param rhs Right-hand operand of the binary expression.
/// @param fmt Optional `printf`-style format string (printed only on failure).
/// @param ... Optional variadic arguments for @p fmt.
///
/// @par Example — basic numeric comparison:
/// @code{.c}
/// int a = 5, b = 10;
/// test_op(a, <, b);
/// @endcode
///
/// @par Example — with custom failure message:
/// @code{.c}
/// int value = get_value();
/// test_op(value, >=, 0, "Value out of range: %d", value);
/// @endcode
///
/// @par Example — string comparison:
/// @code{.c}
/// const char *s = get_name();
/// test_op(strcmp(s, "admin"), ==, 0, "Unexpected user: %s", s);
/// @endcode
///
/// @note The operator must be written as a C token, not a string. For example,
///       `test_op(x, ==, y)` is correct; `test_op(x, "==", y)` will **not**
///       compile and would produce a misleading report even if it did.
///
/// @see test For unary truthiness assertions.
#define test_op(...)                                                           \
  GREJC_DISPATCH_OP(GREJC_VA_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)

#endif /* NDEBUG */

// ============================================================================
// BOX UTILS
// ============================================================================

/// @defgroup box_chars Box Drawing Characters
///
/// Single-line box drawing characters using Unicode box-drawing symbols.
///
/// @{
#define BOX_H "\u2500"     ///< Horizontal line.
#define BOX_V "\u2502"     ///< Vertical line.
#define BOX_TL "\u250C"    ///< Top-left corner.
#define BOX_TR "\u2510"    ///< Top-right corner.
#define BOX_BL "\u2514"    ///< Bottom-left corner.
#define BOX_BR "\u2518"    ///< Bottom-right corner.
#define BOX_ML "\u251C"    ///< Middle-left (T-junction pointing right).
#define BOX_MR "\u2524"    ///< Middle-right (T-junction pointing left).
#define BOX_MT "\u252C"    ///< Middle-top (T-junction pointing down).
#define BOX_MB "\u2534"    ///< Middle-bottom (T-junction pointing up).
#define BOX_CROSS "\u253C" ///< Cross intersection.
/// @}

/// @defgroup box2_chars Double-line Box Drawing Characters
///
/// @{
#define BOX2_H "\u2550"  ///< Double horizontal line.
#define BOX2_V "\u2551"  ///< Double vertical line.
#define BOX2_TL "\u2554" ///< Double top-left corner.
#define BOX2_TR "\u2557" ///< Double top-right corner.
#define BOX2_BL "\u255A" ///< Double bottom-left corner.
#define BOX2_BR "\u255D" ///< Double bottom-right corner.
/// @}

/// @brief Prints a horizontal line composed of repeated characters.
///
/// Internal helper used by the box and table macros. Prints @p left, followed
/// by @p width repetitions of @p ch, followed by @p right and a newline.
///
/// @param left  String printed before the line (e.g. a corner character).
/// @param ch    Character (or multi-char string) repeated across the line.
/// @param right String printed after the line.
/// @param width Number of repetitions of @p ch.
static inline void _box_hline(const char *left, const char *ch,
                              const char *right, int width) {
  printf("%s", left);
  for (int i = 0; i < width; i++)
    printf("%s", ch);
  printf("%s\n", right);
}

/// @brief Prints @p text centered inside a single-line box.
///
/// The box width is determined by the terminal width. The text is padded with
/// one space on each side.
///
/// @param text  The string to display.
/// @param color A @ref TERMINAL_COLOR_* constant for the box border, or
///              @ref TERMINAL_COLOR_RESET for default.
///
/// Example:
/// @code{.c}
/// print_box("Hello, World!", TERMINAL_COLOR_CYAN);
/// @endcode
static inline void print_box(const char *text, const char *color) {
  int term_w = _get_terminal_dimensions().width;
  int t_len = strlen(text);
  int pad_left = (term_w - 2 - t_len) / 2;
  if (pad_left < 0)
    pad_left = 0;
  int pad_right = term_w - 2 - t_len - pad_left;
  if (pad_right < 0)
    pad_right = 0;

  printf("%s", color);
  _box_hline(BOX_TL, BOX_H, BOX_TR, term_w - 2);

  printf("%s%s%*s%s%*s%s\n", BOX_V, TERMINAL_COLOR_RESET, pad_left + t_len,
         text, color, pad_right, "", BOX_V);

  _box_hline(BOX_BL, BOX_H, BOX_BR, term_w - 2);
  printf("%s", TERMINAL_COLOR_RESET);
}

/// @brief Prints @p text centered inside a double-line box.
///
/// Identical behaviour to @ref print_box but uses double-line border
/// characters (@ref BOX2_*) instead of single-line ones.
///
/// @param text  The string to display.
/// @param color A @ref TERMINAL_COLOR_* constant for the border.
///
/// Example:
/// @code{.c}
/// print_box_double("Hello, World!", TERMINAL_COLOR_CYAN);
/// @endcode
static inline void print_box_double(const char *text, const char *color) {
  int term_w = _get_terminal_dimensions().width;
  int t_len = strlen(text);
  int pad_left = (term_w - 2 - t_len) / 2;
  if (pad_left < 0)
    pad_left = 0;
  int pad_right = term_w - 2 - t_len - pad_left;
  if (pad_right < 0)
    pad_right = 0;

  printf("%s", color);
  _box_hline(BOX2_TL, BOX2_H, BOX2_TR, term_w - 2);

  printf("%s%s%*s%s%*s%s\n", BOX2_V, TERMINAL_COLOR_RESET, pad_left + t_len,
         text, color, pad_right, "", BOX2_V);

  _box_hline(BOX2_BL, BOX2_H, BOX2_BR, term_w - 2);
  printf("%s", TERMINAL_COLOR_RESET);
}

// ============================================================================
// TABLE UTILS
// ============================================================================

/// @brief Maximum number of columns supported by the table API.
#define TABLE_MAX_COLS 16

/// @brief Configuration style for a table.
///
/// Stores the column layout, the number of columns, and the colours to use for
/// the header, data rows and borders. Initialise with @ref table_init and
/// optionally override individual column widths with @ref table_set_col_width.
typedef struct {
  int col_count;                  ///< Number of columns in use.
  int col_widths[TABLE_MAX_COLS]; ///< Inner width per column (no padding).
  const char *header_color;       ///< Colour for header text.
  const char *row_color;          ///< Colour for data row text.
  const char *border_color;       ///< Colour for border characters.
} TableStyle;

/// @brief Initialises a @ref TableStyle, distributing available terminal width
///        evenly among @p col_count columns.
///
/// The available width is computed as the terminal width minus the space needed
/// for borders (one per column plus one outer) and padding (two spaces per
/// column). If the terminal is very narrow, each column is given at least 1
/// character of width.
///
/// @param style        Pointer to an uninitialised @ref TableStyle.
/// @param col_count    Number of columns (maximum @ref TABLE_MAX_COLS).
/// @param header_color Colour for header text (e.g. @ref TERMINAL_COLOR_CYAN).
/// @param row_color    Colour for data row text.
/// @param border_color Colour for border characters.
///
/// Example:
/// @code{.c}
/// TableStyle ts;
/// table_init(&ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE,
///                   TERMINAL_COLOR_BLUE);
/// @endcode
static inline void table_init(TableStyle *style, int col_count,
                              const char *header_color, const char *row_color,
                              const char *border_color) {
  const int TERMINAL_WIDTH = _get_terminal_dimensions().width;
  if (col_count > TABLE_MAX_COLS)
    col_count = TABLE_MAX_COLS;
  style->col_count = col_count;
  style->header_color = header_color ? header_color : TERMINAL_COLOR_RESET;
  style->row_color = row_color ? row_color : TERMINAL_COLOR_RESET;
  style->border_color = border_color ? border_color : TERMINAL_COLOR_RESET;

  int term_w = TERMINAL_WIDTH > 0 ? TERMINAL_WIDTH : 80;
  /* borders: 1(left) + col_count*(1 right) = col_count+1 chars
     padding: col_count * 2 (one space each side) */
  int avail = term_w - (col_count + 1) - (col_count * 2);
  if (avail < col_count)
    avail = col_count;
  int base = avail / col_count;
  int extra = avail % col_count;
  for (int i = 0; i < col_count; i++)
    style->col_widths[i] = base + (i < extra ? 1 : 0);
}

/// @brief Overrides the display width of a specific column.
///
/// Use after @ref table_init to give a column a fixed width instead of the
/// evenly-distributed default.
///
/// @param style Pointer to an initialised @ref TableStyle.
/// @param col   Column index (0-based).
/// @param width Desired inner width in characters.
static inline void table_set_col_width(TableStyle *style, int col, int width) {
  if (col >= 0 && col < style->col_count)
    style->col_widths[col] = width;
}

/// @brief Internal: prints one horizontal separator row for the table.
///
/// @param s     Pointer to the @ref TableStyle (provides column widths +
/// colour).
/// @param left  Leftmost character (e.g. @ref BOX_TL, @ref BOX_ML, @ref
/// BOX_BL).
/// @param mid   Column separator character (e.g. @ref BOX_MT, @ref BOX_CROSS,
/// @ref BOX_MB).
/// @param right Rightmost character (e.g. @ref BOX_TR, @ref BOX_MR, @ref
/// BOX_BR).
static inline void _table_hline(const TableStyle *s, const char *left,
                                const char *mid, const char *right) {
  printf("%s%s", s->border_color, left);
  for (int c = 0; c < s->col_count; c++) {
    for (int i = 0; i < s->col_widths[c] + 2; i++)
      printf("%s", BOX_H);
    printf("%s", c < s->col_count - 1 ? mid : right);
  }
  printf("%s\n", TERMINAL_COLOR_RESET);
}

/// @brief Internal: prints one data row for the table.
///
/// Each cell is truncated or padded to the column width, with one space of
/// padding on each side. NULL cell pointers are displayed as empty strings.
///
/// @param s          Pointer to the @ref TableStyle.
/// @param cells      Array of @p col_count strings to display.
/// @param text_color Colour for the cell text (header or row colour).
static inline void _table_row(const TableStyle *s, const char *const *cells,
                              const char *text_color) {
  printf("%s%s%s", s->border_color, BOX_V, TERMINAL_COLOR_RESET);
  for (int c = 0; c < s->col_count; c++) {
    int w = s->col_widths[c];
    const char *cell = cells[c] ? cells[c] : "";

    printf("%s %-*.*s %s%s%s", text_color, w, w, cell, s->border_color, BOX_V,
           TERMINAL_COLOR_RESET);
  }
  putchar('\n');
}

/// @brief Prints the top border and header row of a table.
///
/// Must be called once before any data rows.
///
/// @param style   Pointer to an initialised @ref TableStyle.
/// @param headers Array of @p col_count header strings.
///
/// Example:
/// @code{.c}
/// TableStyle ts;
/// table_init(&ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE,
///                   TERMINAL_COLOR_BLUE);
/// const char *h[] = {"Name", "Age", "City"};
/// table_print_header(&ts, h);
/// @endcode
static inline void table_print_header(const TableStyle *style,
                                      const char *const *headers) {
  _table_hline(style, BOX_TL, BOX_MT, BOX_TR);
  _table_row(style, headers, style->header_color);
  _table_hline(style, BOX_ML, BOX_CROSS, BOX_MR);
}

/// @brief Prints a single data row.
///
/// @param style Pointer to an initialised @ref TableStyle.
/// @param cells Array of @p col_count cell strings.
///
/// Example:
/// @code{.c}
/// const char *row[] = {"Alice", "30", "New York"};
/// table_print_row(&ts, row);
/// @endcode
static inline void table_print_row(const TableStyle *style,
                                   const char *const *cells) {
  _table_row(style, cells, style->row_color);
}

/// @brief Prints the bottom border of a table.
///
/// Call after the last data row.
///
/// @param style Pointer to an initialised @ref TableStyle.
///
/// Example:
/// @code{.c}
/// table_print_footer(&ts);
/// @endcode
static inline void table_print_footer(const TableStyle *style) {
  _table_hline(style, BOX_BL, BOX_MB, BOX_BR);
}

// ============================================================================
// MENU UTILS
// ============================================================================

/// @brief Reads a single keypress without waiting for Enter (raw mode).
///
/// Puts the terminal into non-canonical, no-echo mode, reads one character,
/// then restores the original terminal settings.
///
/// Arrow keys are detected via the `\033[` escape sequence and mapped to
/// special integer constants.
///
/// @return The ASCII value of the key pressed, or one of:
///         | Value | Meaning      |
///         |-------|--------------|
///         |  -1   | Error        |
///         |  256  | Arrow Up     |
///         |  257  | Arrow Down   |
///         |  258  | Arrow Left   |
///         |  259  | Arrow Right  |
static inline int _menu_read_key(void) {
  struct termios oldt, newt;
  if (tcgetattr(STDIN_FILENO, &oldt) != 0)
    return -1;
  newt = oldt;
  newt.c_lflag &= ~(unsigned)(ICANON | ECHO);
  newt.c_cc[VMIN] = 1;
  newt.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  int ch = getchar();
  int result = ch;
  if (ch == '\033') { /* ESC sequence */
    int c2 = getchar();
    if (c2 == '[') {
      int c3 = getchar();
      switch (c3) {
      case 'A':
        result = 256;
        break; /* Up    */
      case 'B':
        result = 257;
        break; /* Down  */
      case 'C':
        result = 259;
        break; /* Right */
      case 'D':
        result = 258;
        break; /* Left  */
      default:
        result = c3;
        break;
      }
    } else {
      result = c2;
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return result;
}

/// @brief Displays an interactive arrow-key menu and returns the user's choice.
///
/// Renders a list of @p items on the terminal. The user navigates with the Up
/// and Down arrow keys (or 'k'/'j' Vim-style), confirms with Enter/Space, or
/// cancels with 'q' or Escape.
///
/// The caller is responsible for clearing or redrawing the surrounding UI
/// before and after the menu, as the function does not clear the screen.
///
/// @param title        Title string printed above the menu (NULL for no title).
/// @param items        Array of @p count option strings.
/// @param count        Number of options in @p items.
/// @param title_color  @ref TERMINAL_COLOR_* for the title text.
/// @param cursor_color @ref TERMINAL_COLOR_* for the currently highlighted
/// item.
/// @param item_color   @ref TERMINAL_COLOR_* for unselected items.
/// @param cursor_char  String drawn before the active item (e.g. `"> "` or
/// `"\u25b6 "`).
///
/// @return The 0-based index of the selected item, or -1 if the user cancelled
///         (pressed 'q' or Escape).
///
/// Example:
/// @code{.c}
/// const char *opts[] = {"New Game", "Load Game", "Options", "Quit"};
/// int choice = menu_select("MAIN MENU", opts, 4,
///                          TERMINAL_COLOR_CYAN,
///                          TERMINAL_COLOR_GREEN,
///                          TERMINAL_COLOR_WHITE,
///                          "\u25b6 ");
/// @endcode
static inline int menu_select(const char *title, const char *const *items,
                              int count, const char *title_color,
                              const char *cursor_color, const char *item_color,
                              const char *cursor_char) {
  if (!cursor_char)
    cursor_char = "> ";
  int cursor_len = 0;
  for (const char *p = cursor_char; *p; p++)
    cursor_len++;

  int selected = 0;

  /* hide cursor while navigating */
  printf("\033[?25l");

  while (1) {
    /* save cursor position so we can redraw in-place */
    printf("\033[s");

    if (title) {
      printf("%s%s%s\n", title_color ? title_color : "", title,
             TERMINAL_COLOR_RESET);
    }

    for (int i = 0; i < count; i++) {
      if (i == selected) {
        printf("%s%s%s%s\n", cursor_color ? cursor_color : "", cursor_char,
               items[i], TERMINAL_COLOR_RESET);
      } else {
        /* indent by cursor width */
        printf("%s", item_color ? item_color : "");
        for (int s = 0; s < cursor_len; s++)
          putchar(' ');
        printf("%s%s\n", items[i], TERMINAL_COLOR_RESET);
      }
    }
    fflush(stdout);

    int key = _menu_read_key();
    if (key == 256 /* Up */ || key == 'k') {
      selected = (selected - 1 + count) % count;
    } else if (key == 257 /* Down */ || key == 'j') {
      selected = (selected + 1) % count;
    } else if (key == '\n' || key == '\r' || key == ' ') {
      break;
    } else if (key == 'q' || key == 27 /* ESC */) {
      selected = -1;
      break;
    }

    /* restore cursor to saved position and overwrite the menu */
    printf("\033[u");
  }

  /* show cursor again */
  printf("\033[?25h");
  return selected;
}

/// @brief Convenience wrapper that renders a double-line box title, then the
///        interactive menu.
///
/// Combines @ref print_box_double with @ref menu_select for quick, polished
/// menus. The @p accent_color is used for both the box border and the
/// selection cursor.
///
/// @param title        Title for the box header.
/// @param items        Array of option strings.
/// @param count        Number of options.
/// @param accent_color @ref TERMINAL_COLOR_* used for both the box border and
///                     the highlighted item cursor.
///
/// @return The 0-based index of the selected item, or -1 if cancelled.
///
/// Example:
/// @code{.c}
/// const char *opts[] = {"Start", "Settings", "Exit"};
/// int ch = menu_select_boxed("LAUNCHER", opts, 3, TERMINAL_COLOR_CYAN);
/// @endcode
static inline int menu_select_boxed(const char *title, const char *const *items,
                                    int count, const char *accent_color) {
  print_box_double(title, accent_color);
  putchar('\n');
  return menu_select(NULL, items, count, accent_color, accent_color,
                     TERMINAL_COLOR_WHITE, "\u25b6  ");
}

// ============================================================================
// PROGRESS BAR UTILS
// ============================================================================

/// @defgroup progress_styles Progress Bar Style Flags
///
/// Constants for selecting the visual style of a @ref ProgressBar.
///
/// @{
#define PROGRESS_STYLE_SIMPLE 0 ///< `[████░░░░] 75%` — block fill.
#define PROGRESS_STYLE_BLOCKS 1 ///< `[▏▎▍▌▋▊▉█]` — smooth ⅛-step fill.
#define PROGRESS_STYLE_ARROW 2  ///< `[=====>   ]` — classic arrow.
#define PROGRESS_STYLE_DOTS 3   ///< `[•········]` — dot-fill style.
/// @}

/// @brief State for a single progress bar.
///
/// Initialise with @ref progress_bar_init, then set @p value and call
/// @ref progress_bar_print or @ref progress_bar_update.
typedef struct {
  float value;             ///< Progress value, 0.0 to 1.0.
  int width;               ///< Total bar width including brackets; 0 = auto.
  int style;               ///< One of @ref PROGRESS_STYLE_SIMPLE, etc.
  const char *bar_color;   ///< Colour of the filled portion.
  const char *empty_color; ///< Colour of the empty portion.
  const char *label;       ///< Optional text printed after the percentage.
  int show_percent;        ///< If 1, display "XX%" after the bar.
} ProgressBar;

/// @brief Initialises a @ref ProgressBar with sensible defaults.
///
/// Sets value to 0.0, width to auto, percent display on, and applies the
/// given @p style and @p color.
///
/// @param pb    Pointer to a @ref ProgressBar to initialise.
/// @param style One of @ref PROGRESS_STYLE_SIMPLE, @ref PROGRESS_STYLE_BLOCKS,
///              @ref PROGRESS_STYLE_ARROW, or @ref PROGRESS_STYLE_DOTS.
/// @param color @ref TERMINAL_COLOR_* for the filled portion of the bar
///              (e.g. @ref TERMINAL_COLOR_GREEN).
///
/// Example:
/// @code{.c}
/// ProgressBar pb;
/// progress_bar_init(&pb, PROGRESS_STYLE_SIMPLE, TERMINAL_COLOR_GREEN);
/// @endcode
static inline void progress_bar_init(ProgressBar *pb, int style,
                                     const char *color) {
  pb->value = 0.0f;
  pb->width = 0; /* auto */
  pb->style = style;
  pb->bar_color = color ? color : TERMINAL_COLOR_GREEN;
  pb->empty_color = TERMINAL_COLOR_WHITE;
  pb->label = NULL;
  pb->show_percent = 1;
}

/// @brief Internal: renders one progress bar line to stdout (no newline).
///
/// Draws the bar according to the selected style. The bar width is derived
/// from the terminal width minus space reserved for brackets, percentage and
/// label.
///
/// @param pb Pointer to the @ref ProgressBar to render.
static inline void _progress_bar_render(const ProgressBar *pb) {
  const int TERMINAL_WIDTH = _get_terminal_dimensions().width / 1.5;
  int term_w = TERMINAL_WIDTH > 0 ? TERMINAL_WIDTH : 40;

  /* reserve room for "[ ]" (2), " 100%" (5), optional label */
  int label_len = 0;
  if (pb->label)
    for (const char *p = pb->label; *p; p++)
      label_len++;
  int reserved =
      2 + (pb->show_percent ? 5 : 0) + (label_len ? label_len + 1 : 0);
  int bar_w = (pb->width > 0 ? pb->width : term_w) - reserved;
  if (bar_w < 4)
    bar_w = 4;

  float v = pb->value < 0.0f ? 0.0f : (pb->value > 1.0f ? 1.0f : pb->value);

  printf("[");

  if (pb->style == PROGRESS_STYLE_BLOCKS) {
    /* smooth 8-step block fill */
    static const char *const eighths[] = {" ",      "\u258F", "\u258E",
                                          "\u258D", "\u258C", "\u258B",
                                          "\u258A", "\u2589", "\u2588"};
    float cells = v * (float)bar_w;
    int full = (int)cells;
    int frac = (int)((cells - (float)full) * 8.0f);

    printf("%s", pb->bar_color);
    for (int i = 0; i < full; i++)
      printf("\u2588");
    if (full < bar_w) {
      printf("%s", frac > 0 ? pb->bar_color : pb->empty_color);
      printf("%s", eighths[frac]);
      printf("%s", pb->empty_color);
      for (int i = full + 1; i < bar_w; i++)
        printf(" ");
    }

  } else if (pb->style == PROGRESS_STYLE_ARROW) {
    int filled = (int)(v * (float)bar_w);
    printf("%s", pb->bar_color);
    for (int i = 0; i < filled - 1; i++)
      printf("=");
    if (filled > 0 && filled < bar_w)
      printf(">");
    else if (filled > 0)
      printf("=");
    printf("%s", pb->empty_color);
    for (int i = filled; i < bar_w; i++)
      printf(" ");

  } else if (pb->style == PROGRESS_STYLE_DOTS) {
    int filled = (int)(v * (float)bar_w);
    printf("%s", pb->bar_color);
    for (int i = 0; i < filled; i++)
      printf("\u2022");
    printf("%s", pb->empty_color);
    for (int i = filled; i < bar_w; i++)
      printf("\u00B7");

  } else {
    /* PROGRESS_STYLE_SIMPLE — default */
    int filled = (int)(v * (float)bar_w);
    printf("%s", pb->bar_color);
    for (int i = 0; i < filled; i++)
      printf("\u2588");
    printf("%s", pb->empty_color);
    for (int i = filled; i < bar_w; i++)
      printf("\u2591");
  }

  printf("%s]", TERMINAL_COLOR_RESET);

  if (pb->show_percent)
    printf(" %3d%%", (int)(v * 100.0f));
  if (pb->label)
    printf(" %s", pb->label);
}

/// @brief Prints the progress bar followed by a newline.
///
/// Use for static / one-shot display when you just want to show the bar once.
///
/// @param pb Pointer to a @ref ProgressBar (value must be set beforehand).
///
/// Example:
/// @code{.c}
/// ProgressBar pb;
/// progress_bar_init(&pb, PROGRESS_STYLE_BLOCKS, TERMINAL_COLOR_GREEN);
/// pb.value = 0.65f;
/// pb.label = "compiling\u2026";
/// progress_bar_print(&pb);
/// @endcode
static inline void progress_bar_print(const ProgressBar *pb) {
  _progress_bar_render(pb);
  putchar('\n');
}

/// @brief Updates the progress bar **in-place** on the current line.
///
/// Uses `\r` to overwrite the current line, producing a smooth animation.
/// Finishes with a newline when @p value reaches 1.0.
///
/// Call this repeatedly in a loop to animate a live progress indicator.
///
/// @param pb    Pointer to the @ref ProgressBar.
/// @param value New progress value (clamped to 0.0 – 1.0).
///
/// Example:
/// @code{.c}
/// ProgressBar pb;
/// progress_bar_init(&pb, PROGRESS_STYLE_SIMPLE, TERMINAL_COLOR_CYAN);
/// for (int i = 0; i <= 100; i++) {
///     progress_bar_update(&pb, i / 100.0f);
///     usleep(20000);
/// }
/// @endcode
static inline void progress_bar_update(ProgressBar *pb, float value) {
  pb->value = value;
  printf("\r");
  _progress_bar_render(pb);
  if (value >= 1.0f)
    putchar('\n');
  fflush(stdout);
}

// ============================================================================
// ARG PARSER
// ============================================================================
//
// Usage pattern (mirrors Python's argparse):
//
//   ArgParser ap;
//   argp_init(&ap, "mytool", "1.0", "Does something useful.");
//
//   argp_add_flag  (&ap, 'v', "verbose", "Enable verbose output");
//   argp_add_option(&ap, 'o', "output",  "FILE",   "Output file path", NULL);
//   argp_add_option(&ap, 'n', "count",   "NUMBER", "Repeat N times",   "1");
//   argp_add_pos   (&ap, "input", "Input file", 1 /* required */);
//
//   if (!argp_parse(&ap, argc, argv)) {
//       argp_usage(&ap);   // prints help and exits
//   }
//
//   int   verbose = argp_flag(&ap, "verbose");
//   const char *out   = argp_get (&ap, "output");
//   int   count = atoi(argp_get(&ap, "count"));
//   const char *input = argp_pos (&ap, "input");
//
//   argp_free(&ap);
//
// Supported syntax (same as standard POSIX / GNU style):
//   -v                  short flag
//   -o file             short option with value
//   --verbose           long flag
//   --output=file       long option, = form
//   --output file       long option, space form
//   --                  stop option parsing; rest are positional

/// @brief Maximum number of arguments (flags + options + positional)
///        that can be registered with @ref argp_add_flag, etc.
#define ARGP_MAX_ARGS 64

/// @brief Maximum number of positional arguments that can be registered
///        with @ref argp_add_pos.
#define ARGP_MAX_POS 16

/// @brief Maximum length of a single argument value string (including the
///        null terminator).
#define ARGP_VAL_LEN 256

/// @brief Discriminator for the type of an argument in the ArgParser system.
typedef enum {
  ARGP_KIND_FLAG,   ///< Boolean switch, no value  (e.g. `--verbose`).
  ARGP_KIND_OPTION, ///< Option that takes a value (e.g. `--output=X`).
  ARGP_KIND_POS     ///< Positional argument        (e.g. `<input>`).
} ArgKind;

/// @brief Describes a single registered argument.
///
/// Fields are filled by the registration functions (@ref argp_add_flag,
/// @ref argp_add_option, @ref argp_add_pos) and populated during parsing
/// by @ref argp_parse.
typedef struct {
  ArgKind kind;             ///< Type: flag, option or positional.
  char shortname;           ///< Single-character short name ('\0' if none).
  char longname[32];        ///< Long name without the "--" prefix.
  char metavar[32];         ///< For OPTION: value placeholder (e.g. "FILE").
  char help[300];           ///< Description shown in `--help`.
  char value[ARGP_VAL_LEN]; ///< Parsed value string (or default).
  int present;              ///< 1 if the argument was supplied by the user.
  int required;             ///< 1 if absence is a parse error.
} Arg;

/// @brief Parser state for a Python-style argument parser.
///
/// Initialise with @ref argp_init, register arguments with @ref argp_add_flag
/// etc., then parse with @ref argp_parse.
typedef struct {
  char prog[64];           ///< Program name, shown in usage line.
  char version[32];        ///< Version string, shown with `--version`.
  char description[256];   ///< Short description printed below usage.
  Arg args[ARGP_MAX_ARGS]; ///< Array of registered arguments.
  int count;               ///< Number of registered arguments so far.
  int pos_count;           ///< Number of registered positional slots.
  char error[256];         ///< Last parse error message (human-readable).
} ArgParser;

/// @brief Initialises an @ref ArgParser.
///
/// Zeroes out the parser structure, copies @p prog, @p version and
/// @p description, and automatically registers built-in `--help` and
/// `--version` flags.
///
/// @param ap          Pointer to an uninitialised @ref ArgParser.
/// @param prog        Program name (shown in the usage line).
/// @param version     Version string (shown with `--version`).
/// @param description Short description printed below the usage line.
///
/// Example:
/// @code{.c}
/// ArgParser ap;
/// argp_init(&ap, "mytool", "1.0", "Does something useful.");
/// @endcode
static inline void argp_init(ArgParser *ap, const char *prog,
                             const char *version, const char *description) {
  memset(ap, 0, sizeof(ArgParser));
  strncpy(ap->prog, prog, sizeof(ap->prog) - 1);
  strncpy(ap->version, version, sizeof(ap->version) - 1);
  strncpy(ap->description, description, sizeof(ap->description) - 1);

  /* built-in --help / --version */
  Arg *h = &ap->args[ap->count++];
  h->kind = ARGP_KIND_FLAG;
  h->shortname = 'h';
  strncpy(h->longname, "help", sizeof(h->longname) - 1);
  strncpy(h->help, "Show this help message and exit", sizeof(h->help) - 1);

  Arg *ver = &ap->args[ap->count++];
  ver->kind = ARGP_KIND_FLAG;
  ver->shortname = '\0';
  strncpy(ver->longname, "version", sizeof(ver->longname) - 1);
  strncpy(ver->help, "Show version and exit", sizeof(ver->help) - 1);
}

/// @brief Internal: finds a registered @ref Arg by its long name.
///
/// @param ap   Pointer to the @ref ArgParser.
/// @param name Long name to search for (without "--").
/// @return Pointer to the matching @ref Arg, or NULL if not found.
static inline Arg *_argp_find(ArgParser *ap, const char *name) {
  for (int i = 0; i < ap->count; i++)
    if (strcmp(ap->args[i].longname, name) == 0)
      return &ap->args[i];
  return NULL;
}

/// @brief Internal: finds a registered @ref Arg by its short name.
///
/// @param ap Pointer to the @ref ArgParser.
/// @param c  Single-character short name.
/// @return Pointer to the matching @ref Arg, or NULL if not found.
static inline Arg *_argp_find_short(ArgParser *ap, char c) {
  for (int i = 0; i < ap->count; i++)
    if (ap->args[i].shortname == c)
      return &ap->args[i];
  return NULL;
}

/// @brief Registers a boolean flag (e.g. `--verbose` / `-v`).
///
/// Flags are either present or absent. After parsing, retrieve with
/// @ref argp_flag. The stored value is "1" when present, "0" otherwise.
///
/// @param ap        Pointer to an initialised @ref ArgParser.
/// @param shortname Single character (e.g. 'v'), or '\0' for no short form.
/// @param longname  Long name without the "--" prefix (e.g. "verbose").
/// @param help      Help string displayed in `--help`.
///
/// Example:
/// @code{.c}
/// argp_add_flag(&ap, 'v', "verbose", "Enable verbose output");
/// // Later: if (argp_flag(&ap, "verbose")) { ... }
/// @endcode
static inline void argp_add_flag(ArgParser *ap, char shortname,
                                 const char *longname, const char *help) {
  if (ap->count >= ARGP_MAX_ARGS)
    return;
  Arg *a = &ap->args[ap->count++];
  memset(a, 0, sizeof(Arg));
  a->kind = ARGP_KIND_FLAG;
  a->shortname = shortname;
  strncpy(a->longname, longname, sizeof(a->longname) - 1);
  strncpy(a->help, help, sizeof(a->help) - 1);
  strncpy(a->value, "0", sizeof(a->value) - 1);
}

/// @brief Registers an option that accepts a user-supplied value
///        (e.g. `--output=FILE` or `-o FILE`).
///
/// The option can be specified on the command line in three forms:
///   - `--longname=value`   (long form with equals sign)
///   - `--longname value`   (long form with space)
///   - `-s value`           (short form with space)
///
/// If @p def is non-NULL, the option is *optional* and defaults to that
/// string; the `required` field is automatically set to 0. If @p def is NULL,
/// the option is *required* and parsing will fail if the user does not supply
/// it. The caller may override `a->required = 0` after registration to make a
/// NULL-default option optional (value will be an empty string if omitted).
///
/// After parsing, retrieve the value with @ref argp_get.
///
/// @param ap        Pointer to an initialised @ref ArgParser.
/// @param shortname Single-character short name (e.g. 'o'), or '\0' for none.
/// @param longname  Long option name without the "--" prefix (e.g. "output").
/// @param metavar   Placeholder string shown in `--help` (e.g. "FILE", "N").
/// @param help      Description of what this option controls.
/// @param def       Default value string, or NULL to make the option required.
///
/// Example:
/// @code{.c}
/// argp_add_option(&ap, 'o', "output", "FILE", "Output file path", "./out");
/// argp_add_option(&ap, 'n', "count",  "N",    "Number of iterations", "10");
/// // required option (no default):
/// argp_add_option(&ap, 'i', "input",  "FILE", "Input file (required)", NULL);
/// @endcode
static inline void argp_add_option(ArgParser *ap, char shortname,
                                   const char *longname, const char *metavar,
                                   const char *help, const char *def) {
  if (ap->count >= ARGP_MAX_ARGS)
    return;
  Arg *a = &ap->args[ap->count++];
  memset(a, 0, sizeof(Arg));
  a->kind = ARGP_KIND_OPTION;
  a->shortname = shortname;
  a->required = (def == NULL) ? 1 : 0;
  strncpy(a->longname, longname, sizeof(a->longname) - 1);
  strncpy(a->metavar, metavar, sizeof(a->metavar) - 1);
  strncpy(a->help, help, sizeof(a->help) - 1);
  if (def)
    strncpy(a->value, def, sizeof(a->value) - 1);
}

/// @brief Registers a positional argument (e.g. `<input>`).
///
/// Positional arguments are filled in the order they were registered. Retrieve
/// parsed values with @ref argp_pos.
///
/// @param ap       Pointer to an initialised @ref ArgParser.
/// @param name     Name used in help and for @ref argp_pos retrieval.
/// @param help     Help string displayed in `--help`.
/// @param required 1 = error if absent, 0 = optional.
///
/// Example:
/// @code{.c}
/// argp_add_pos(&ap, "input", "Input file path", 1);
/// @endcode
static inline void argp_add_pos(ArgParser *ap, const char *name,
                                const char *help, int required) {
  if (ap->count >= ARGP_MAX_ARGS || ap->pos_count >= ARGP_MAX_POS)
    return;
  Arg *a = &ap->args[ap->count++];
  memset(a, 0, sizeof(Arg));
  a->kind = ARGP_KIND_POS;
  a->required = required;
  ap->pos_count++;
  strncpy(a->longname, name, sizeof(a->longname) - 1);
  strncpy(a->help, help, sizeof(a->help) - 1);
}

/// @brief Prints a formatted help message to stdout and exits with code 0.
///
/// The output includes:
/// - A usage line showing the program name and all registered arguments.
/// - The program description (if set).
/// - A formatted list of options with their short/long names, metavar, help
///   text and default values (if any).
/// - A list of positional arguments (if any), marked as optional or required.
///
/// After printing, the function calls `exit(0)`.
///
/// @param ap Pointer to an initialised and populated @ref ArgParser.
static inline void argp_usage(ArgParser *ap) {
  /* usage line */
  printf("\n%sUsage:%s %s%s%s", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET,
         TERMINAL_COLOR_GREEN, ap->prog, TERMINAL_COLOR_RESET);

  for (int i = 0; i < ap->count; i++) {
    Arg *a = &ap->args[i];
    if (a->kind == ARGP_KIND_FLAG || a->kind == ARGP_KIND_OPTION) {
      printf(" [");
      if (a->shortname)
        printf("-%c", a->shortname);
      else
        printf("--%s", a->longname);
      if (a->kind == ARGP_KIND_OPTION)
        printf(" %s", a->metavar);
      printf("]");
    }
  }
  for (int i = 0; i < ap->count; i++) {
    Arg *a = &ap->args[i];
    if (a->kind == ARGP_KIND_POS)
      printf(a->required ? " <%s>" : " [%s]", a->longname);
  }
  printf("\n\n");

  /* description */
  if (ap->description[0])
    printf("%s\n\n", ap->description);

  /* column width for alignment */
  int col = 0;
  for (int i = 0; i < ap->count; i++) {
    Arg *a = &ap->args[i];
    if (a->kind == ARGP_KIND_POS)
      continue;
    int w = (int)strlen(a->longname) + 2; /* "--" */
    if (a->shortname)
      w += 4; /* "-x, " */
    if (a->kind == ARGP_KIND_OPTION)
      w += (int)strlen(a->metavar) + 1;
    if (w > col)
      col = w;
  }
  col += 2;

  /* options section */
  printf("%sOptions:%s\n", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET);
  for (int i = 0; i < ap->count; i++) {
    Arg *a = &ap->args[i];
    if (a->kind == ARGP_KIND_POS)
      continue;

    int written = 0;
    printf("  %s", TERMINAL_COLOR_CYAN);
    if (a->shortname) {
      printf("-%c, ", a->shortname);
      written += 4;
    }
    printf("--%s", a->longname);
    written += 2 + (int)strlen(a->longname);
    if (a->kind == ARGP_KIND_OPTION) {
      printf(" %s", a->metavar);
      written += 1 + (int)strlen(a->metavar);
    }
    printf("%s", TERMINAL_COLOR_RESET);
    for (int s = written; s < col; s++)
      putchar(' ');
    printf("%s", a->help);

    /* show default for options */
    if (a->kind == ARGP_KIND_OPTION && !a->required && a->value[0])
      printf("%s (default: %s)%s", TERMINAL_COLOR_BLUE, a->value,
             TERMINAL_COLOR_RESET);
    putchar('\n');
  }

  /* positional section */
  int has_pos = 0;
  for (int i = 0; i < ap->count; i++)
    if (ap->args[i].kind == ARGP_KIND_POS) {
      has_pos = 1;
      break;
    }

  if (has_pos) {
    printf("\n%sPositional arguments:%s\n", TERMINAL_COLOR_YELLOW,
           TERMINAL_COLOR_RESET);
    for (int i = 0; i < ap->count; i++) {
      Arg *a = &ap->args[i];
      if (a->kind != ARGP_KIND_POS)
        continue;
      printf("  %s%-*s%s  %s", TERMINAL_COLOR_CYAN, col, a->longname,
             TERMINAL_COLOR_RESET, a->help);
      if (!a->required)
        printf("%s (optional)%s", TERMINAL_COLOR_BLUE, TERMINAL_COLOR_RESET);
      putchar('\n');
    }
  }
  putchar('\n');
  exit(0);
}

/// @brief Parses command-line arguments according to the registered
///        configuration.
///
/// Iterates over @p argc/@p argv, matching tokens against registered flags,
/// options and positional arguments. Supports:
/// - Short flags: `-v`, `-xvf` (clustering).
/// - Short options with value: `-o file`.
/// - Long flags: `--verbose`.
/// - Long options: `--output=file` or `--output file`.
/// - `--` separator to stop option parsing; remaining tokens are positional.
///
/// Built-in `--help` and `--version` are handled automatically and cause the
/// program to exit.
///
/// @param ap   Pointer to an initialised, fully-registered @ref ArgParser.
/// @param argc Argument count from `main()`.
/// @param argv Argument vector from `main()`.
///
/// @return 1 on success, 0 on error. On error, a human-readable message is
///         stored in @p ap->error which can be printed with
///         @ref argp_print_error.
///
/// Example:
/// @code{.c}
/// if (!argp_parse(&ap, argc, argv)) {
///     argp_print_error(&ap);
///     return 1;
/// }
/// @endcode
static inline int argp_parse(ArgParser *ap, int argc, char *const argv[]) {
  int pos_idx = 0;  /* which positional slot we're filling */
  int only_pos = 0; /* set to 1 after "--" */

  /* collect positional Arg pointers in order */
  Arg *positionals[ARGP_MAX_POS];
  int npos = 0;
  for (int i = 0; i < ap->count; i++)
    if (ap->args[i].kind == ARGP_KIND_POS)
      positionals[npos++] = &ap->args[i];

  for (int i = 1; i < argc; i++) {
    const char *tok = argv[i];

    /* "--" separator */
    if (!only_pos && strcmp(tok, "--") == 0) {
      only_pos = 1;
      continue;
    }

    /* positional */
    if (only_pos || tok[0] != '-' || tok[1] == '\0') {
      if (pos_idx >= npos) {
        snprintf(ap->error, sizeof(ap->error),
                 "unexpected positional argument: %s", tok);
        return 0;
      }
      strncpy(positionals[pos_idx]->value, tok, ARGP_VAL_LEN - 1);
      positionals[pos_idx]->present = 1;
      pos_idx++;
      continue;
    }

    /* long option: --name or --name=value */
    if (tok[1] == '-') {
      const char *name = tok + 2;
      char namebuf[32] = {0};
      const char *eq = strchr(name, '=');
      if (eq) {
        int nlen = (int)(eq - name);
        if (nlen >= (int)sizeof(namebuf))
          nlen = (int)sizeof(namebuf) - 1;
        strncpy(namebuf, name, (size_t)nlen);
        name = namebuf;
      }

      Arg *a = _argp_find(ap, name);
      if (!a) {
        snprintf(ap->error, sizeof(ap->error), "unknown option: --%s", name);
        return 0;
      }

      /* handle --help / --version */
      if (strcmp(name, "help") == 0)
        argp_usage(ap);
      if (strcmp(name, "version") == 0) {
        printf("%s %s\n", ap->prog, ap->version);
        exit(0);
      }

      if (a->kind == ARGP_KIND_FLAG) {
        strncpy(a->value, "1", sizeof(a->value) - 1);
        a->present = 1;
      } else {
        const char *val = eq ? eq + 1 : (i + 1 < argc ? argv[++i] : NULL);
        if (!val) {
          snprintf(ap->error, sizeof(ap->error), "option --%s requires a value",
                   name);
          return 0;
        }
        strncpy(a->value, val, ARGP_VAL_LEN - 1);
        a->present = 1;
      }
      continue;
    }

    /* short options: -v, -o val, -xvf (flag cluster) */
    const char *p = tok + 1;
    while (*p) {
      Arg *a = _argp_find_short(ap, *p);
      if (!a) {
        snprintf(ap->error, sizeof(ap->error), "unknown option: -%c", *p);
        return 0;
      }
      if (strcmp(a->longname, "help") == 0)
        argp_usage(ap);
      if (strcmp(a->longname, "version") == 0) {
        printf("%s %s\n", ap->prog, ap->version);
        exit(0);
      }

      if (a->kind == ARGP_KIND_FLAG) {
        strncpy(a->value, "1", sizeof(a->value) - 1);
        a->present = 1;
        p++;
      } else {
        /* value is the rest of the token or next argv */
        const char *val =
            (p[1] != '\0') ? p + 1 : (i + 1 < argc ? argv[++i] : NULL);
        if (!val) {
          snprintf(ap->error, sizeof(ap->error), "option -%c requires a value",
                   *p);
          return 0;
        }
        strncpy(a->value, val, ARGP_VAL_LEN - 1);
        a->present = 1;
        break; /* consumed rest of token */
      }
    }
  }

  /* check required arguments */
  for (int i = 0; i < ap->count; i++) {
    Arg *a = &ap->args[i];
    if (a->required && !a->present) {
      if (a->kind == ARGP_KIND_POS)
        snprintf(ap->error, sizeof(ap->error),
                 "required positional argument missing: <%s>", a->longname);
      else
        snprintf(ap->error, sizeof(ap->error), "required option missing: --%s",
                 a->longname);
      return 0;
    }
  }
  return 1;
}

/// @brief Returns the value of a named flag or option as a C string.
///
/// For flags, returns "1" if present or "0" otherwise.
/// For options, returns the user-supplied value or the registered default.
///
/// @param ap   Pointer to the @ref ArgParser after a successful parse.
/// @param name Long name of the argument (without "--").
///
/// @return The value string, or NULL if @p name was never registered.
///
/// Example:
/// @code{.c}
/// const char *out = argp_get(&ap, "output");
/// @endcode
static inline const char *argp_get(const ArgParser *ap, const char *name) {
  for (int i = 0; i < ap->count; i++)
    if (strcmp(ap->args[i].longname, name) == 0)
      return ap->args[i].value;
  return NULL;
}

/// @brief Convenience: returns 1 if a flag was set, 0 otherwise.
///
/// Equivalent to checking whether @ref argp_get returns "1".
///
/// @param ap   Pointer to the @ref ArgParser.
/// @param name Long name of the flag.
///
/// @return 1 if the flag was present on the command line, 0 otherwise.
///
/// Example:
/// @code{.c}
/// if (argp_flag(&ap, "verbose")) {
///     printf("Verbose mode enabled.\n");
/// }
/// @endcode
static inline int argp_flag(const ArgParser *ap, const char *name) {
  const char *v = argp_get(ap, name);
  return v && v[0] == '1';
}

/// @brief Returns the value of a positional argument by name.
///
/// @param ap   Pointer to the @ref ArgParser after a successful parse.
/// @param name Name of the positional argument (as registered with
///             @ref argp_add_pos).
///
/// @return The parsed value string, or NULL if the slot was never filled or
///         @p name was not found.
///
/// Example:
/// @code{.c}
/// const char *input = argp_pos(&ap, "input");
/// @endcode
static inline const char *argp_pos(const ArgParser *ap, const char *name) {
  return argp_get(ap, name);
}

/// @brief Prints the parse error and a short usage hint to stderr.
///
/// Use this after @ref argp_parse returns 0 to inform the user what went wrong
/// and how to get help.
///
/// @param ap Pointer to the @ref ArgParser (error message is in @p ap->error).
///
/// Example:
/// @code{.c}
/// if (!argp_parse(&ap, argc, argv)) {
///     argp_print_error(&ap);
///     return 1;
/// }
/// @endcode
static inline void argp_print_error(const ArgParser *ap) {
  fprintf(stderr, "%s%s: error:%s %s\n", TERMINAL_COLOR_RED, ap->prog,
          TERMINAL_COLOR_RESET, ap->error);
  fprintf(stderr, "Try '%s --help' for more information.\n", ap->prog);
}

/// @brief Frees any resources held by the ArgParser.
///
/// The current implementation uses only stack-allocated memory, so this
/// function is a no-op. It is kept for API symmetry so that future versions
/// that might use heap allocation remain backward-compatible.
///
/// @param ap Pointer to the @ref ArgParser (unused).
static inline void argp_free(ArgParser *ap) { (void)ap; }

#endif /* GREJC_UTILS_H */
