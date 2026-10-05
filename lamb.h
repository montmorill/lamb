// ,---@>
//  W-W'
// Copyright 2025 Alexey Kutepov <reximkut@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#ifndef LAMB_H_
#define LAMB_H_

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define _WINUSER_
#    define _WINGDI_
#    define _IMM_
#    define _WINCON_
#    include <windows.h>
#else
#    include <unistd.h>
#    include <sys/wait.h>
#    include <sys/stat.h>
#endif // _WIN32

#if defined(__GNUC__) || defined(__clang__)
//   https://gcc.gnu.org/onlinedocs/gcc-4.7.2/gcc/Function-Attributes.html
#    ifdef __MINGW_PRINTF_FORMAT
#        define PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__ ((format (__MINGW_PRINTF_FORMAT, STRING_INDEX, FIRST_TO_CHECK)))
#    else
#        define PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__ ((format (printf, STRING_INDEX, FIRST_TO_CHECK)))
#    endif // __MINGW_PRINTF_FORMAT
#else
//   TODO: implement PRINTF_FORMAT for MSVC
#    define PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK)
#endif

#define UNUSED(value) (void)(value)
#define TODO(message) do { fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, message); abort(); } while(0)
#define UNREACHABLE(message) do { fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, message); abort(); } while(0)

#define DA_INIT_CAP 256
#define da_reserve(da, expected_capacity)                                                  \
    do {                                                                                   \
        if ((expected_capacity) > (da)->capacity) {                                        \
            if ((da)->capacity == 0) {                                                     \
                (da)->capacity = DA_INIT_CAP;                                              \
            }                                                                              \
            while ((expected_capacity) > (da)->capacity) {                                 \
                (da)->capacity *= 2;                                                       \
            }                                                                              \
            (da)->items = realloc((da)->items, (da)->capacity * sizeof(*(da)->items));     \
            assert((da)->items != NULL && "Buy more RAM lol");                             \
        }                                                                                  \
    } while (0)

#define da_append(da, item)                  \
    do {                                     \
        da_reserve((da), (da)->count + 1);   \
        (da)->items[(da)->count++] = (item); \
    } while (0)

#define da_delete_at(da, i) \
    do { \
       size_t index = (i); \
       assert(index < (da)->count); \
       memmove(&(da)->items[index], &(da)->items[index + 1], ((da)->count - index - 1)*sizeof(*(da)->items)); \
       (da)->count -= 1; \
    } while(0)

#define sb_append_null(sb) da_append(sb, 0)

// --- Command (process execution) -------------------------------------------------

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Cmd;

bool cmd_run(Cmd *cmd);

// --- String utilities ------------------------------------------------------------

char *copy_string_sized(const char *s, size_t n);
char *copy_string(const char *s);

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} String_Builder;

int sb_appendf(String_Builder *sb, const char *fmt, ...) PRINTF_FORMAT(2, 3);

// RETURNS:
//  0 - file does not exists
//  1 - file exists
// -1 - error while checking if file exists. The error is logged
int file_exists(const char *file_path);
bool read_entire_file(const char *path, String_Builder *sb);
bool write_entire_file(const char *path, const void *data, size_t size);

// --- Symbols ---------------------------------------------------------------------

const char *intern_label(const char *label);

typedef struct {
    // Displayed name of the symbol.
    const char *label;
    // Internal tag that makes two symbols with the same label different if needed.
    // Usually used to obtain a fresh symbol for capture avoiding substitution.
    size_t tag;
} Symbol;

bool symbol_eq(Symbol a, Symbol b);
Symbol symbol(const char *label);
Symbol symbol_fresh(Symbol s);

// --- Expressions -----------------------------------------------------------------

typedef enum {
    EXPR_VAR,
    EXPR_FUN,
    EXPR_APP,
    EXPR_MAG,
} Expr_Kind;

typedef struct {
    size_t unwrap;
} Expr_Index;

typedef struct {
    Expr_Kind kind;
    bool visited;
    bool live;
    union {
        Symbol var;
        const char *mag;
        struct {
            Symbol param;
            Expr_Index body;
        } fun;
        struct {
            Expr_Index lhs;
            Expr_Index rhs;
        } app;
    } as;
} Expr;

Expr_Index alloc_expr(void);
void free_expr(Expr_Index expr);

Expr_Index var(Symbol name);
Expr_Index magic(const char *label);
Expr_Index fun(Symbol param, Expr_Index body);
Expr_Index app(Expr_Index lhs, Expr_Index rhs);

void expr_display(Expr_Index expr, String_Builder *sb);
void dump_expr_ast(Expr_Index expr);
void trace_expr(Expr_Index expr);

bool is_var_free_there(Symbol name, Expr_Index there);
Expr_Index replace(Symbol param, Expr_Index body, Expr_Index arg);
bool eval1(Expr_Index expr, Expr_Index *expr1);

// --- Lexer -----------------------------------------------------------------------

typedef enum {
    TOKEN_INVALID,
    TOKEN_END,
    TOKEN_OPAREN,
    TOKEN_CPAREN,
    TOKEN_LAMBDA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_EQUALS,
    TOKEN_NAME,
    TOKEN_MAGIC,
} Token_Kind;

const char *token_kind_display(Token_Kind kind);

typedef struct {
    size_t pos, bol, row;
} Cur;

typedef struct {
    const char *content;
    size_t count;
    const char *file_path;

    Cur cur;

    Token_Kind token;
    String_Builder string;
    size_t row, col;
} Lexer;

void lexer_init(Lexer *l, const char *content, size_t count, const char *file_path);
void lexer_print_loc(Lexer *l, FILE *stream);
char lexer_curr_char(Lexer *l);
char lexer_next_char(Lexer *l);
void lexer_trim_left(Lexer *l);
bool lexer_starts_with(Lexer *l, const char *prefix);
void lexer_drop_line(Lexer *l);
bool issymbol(int x);
bool lexer_next(Lexer *l);
bool lexer_peek(Lexer *l);
void report_unexpected(Lexer *l, Token_Kind expected);
bool lexer_expect(Lexer *l, Token_Kind expected);

// --- Parser ----------------------------------------------------------------------

bool parse_expr(Lexer *l, Expr_Index *expr);
bool parse_fun(Lexer *l, Expr_Index *expr);
bool parse_primary(Lexer *l, Expr_Index *expr);

// --- Commands --------------------------------------------------------------------

typedef struct {
    const char *name;
    const char *signature;
    const char *description;
} Command;

typedef struct {
    Command *items;
    size_t count;
    size_t capacity;
} Commands;

bool command(Commands *commands, const char *input, const char *name, const char *signature, const char *description);
void print_available_commands(Commands *commands);

// --- Bindings and GC -------------------------------------------------------------

typedef struct {
    Symbol name;
    Expr_Index body;
} Binding;

typedef struct {
    Binding *items;
    size_t count;
    size_t capacity;
} Bindings;

void create_binding(Bindings *bindings, Symbol name, Expr_Index body);
bool create_bindings_from_file(const char *file_path, Bindings *bindings);

void gc_mark(Expr_Index root);
void gc(Expr_Index root, Bindings bindings);

#endif // LAMB_H_
