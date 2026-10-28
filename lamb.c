// ,---@>
//  W-W'
// cc -o lamb lamb.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <wctype.h>

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

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Cmd;

bool cmd_run(Cmd *cmd)
{
    if (cmd->count < 1) {
        fprintf(stderr, "ERROR: Could not run empty command");
        return false;
    }

#ifdef _WIN32
    TODO("cmd_run is not implemented for windows");
#else
    pid_t cpid = fork();
    if (cpid < 0) {
        fprintf(stderr, "ERROR: Could not fork child process: %s", strerror(errno));
        return false;
    }

    if (cpid == 0) {
        // NOTE: This leaks a bit of memory in the child process.
        // But do we actually care? It's a one off leak anyway...
        da_append(cmd, NULL);

        if (execvp(cmd->items[0], (char * const*) cmd->items) < 0) {
            fprintf(stderr, "ERROR: Could not exec child process for %s: %s", cmd->items[0], strerror(errno));
            exit(1);
        }
        UNREACHABLE("cmd_run");
    }

    for (;;) {
        int wstatus = 0;
        if (waitpid(cpid, &wstatus, 0) < 0) {
            fprintf(stderr, "ERROR: Could not wait on command (pid %d): %s", cpid, strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                fprintf(stderr, "ERROR: Command exited with exit code %d", exit_status);
                return false;
            }

            break;
        }

        if (WIFSIGNALED(wstatus)) {
            fprintf(stderr, "ERROR: Command process was terminated by signal %d", WTERMSIG(wstatus));
            return false;
        }
    }

    return cpid;
#endif
}

char *copy_string_sized(const char *s, size_t n)
{
    char *ds = malloc(n + 1);
    assert(ds);
    memcpy(ds, s, n);
    ds[n] = '\0';
    return ds;
}

char *copy_string(const char *s)
{
    return copy_string_sized(s, strlen(s));
}

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} String_Builder;

int sb_appendf(String_Builder *sb, const char *fmt, ...) PRINTF_FORMAT(2, 3);
int sb_appendf(String_Builder *sb, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    // NOTE: the new_capacity needs to be +1 because of the null terminator.
    // However, further below we increase sb->count by n, not n + 1.
    // This is because we don't want the sb to include the null terminator. The user can always sb_append_null() if they want it
    da_reserve(sb, sb->count + n + 1);
    char *dest = sb->items + sb->count;
    va_start(args, fmt);
    vsnprintf(dest, n+1, fmt, args);
    va_end(args);

    sb->count += n;

    return n;
}

// RETURNS:
//  0 - file does not exists
//  1 - file exists
// -1 - error while checking if file exists. The error is logged
int file_exists(const char *file_path)
{
#if _WIN32
    // TODO: distinguish between "does not exists" and other errors
    DWORD dwAttrib = GetFileAttributesA(file_path);
    return dwAttrib != INVALID_FILE_ATTRIBUTES;
#else
    struct stat statbuf;
    if (stat(file_path, &statbuf) < 0) {
        if (errno == ENOENT) return 0;
        fprintf(stderr, "ERROR: Could not check if file %s exists: %s", file_path, strerror(errno));
        return -1;
    }
    return 1;
#endif
}

bool read_entire_file(const char *path, String_Builder *sb)
{
    FILE *f = fopen(path, "rb");
    size_t new_count = 0;
    long long m = 0;
    if (f == NULL)                 goto fail;
    if (fseek(f, 0, SEEK_END) < 0) goto fail;
#ifndef _WIN32
    m = ftell(f);
#else
    m = _ftelli64(f);
#endif
    if (m < 0)                     goto fail;
    if (fseek(f, 0, SEEK_SET) < 0) goto fail;

    new_count = sb->count + m;
    if (new_count > sb->capacity) {
        sb->items = realloc(sb->items, new_count);
        assert(sb->items != NULL && "Buy more RAM lool!!");
        sb->capacity = new_count;
    }

    fread(sb->items + sb->count, m, 1, f);
    if (ferror(f)) {
        // TODO: Afaik, ferror does not set errno. So the error reporting in fail is not correct in this case.
        goto fail;
    }
    sb->count = new_count;

    fclose(f);
    return true;
fail:
    fprintf(stderr, "ERROR: Could not read file %s: %s\n", path, strerror(errno));
    if (f) fclose(f);
    return false;
}

bool write_entire_file(const char *path, const void *data, size_t size)
{
    const char *buf = NULL;
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: Could not open file %s for writing: %s\n", path, strerror(errno));
        goto fail;
    }

    //           len
    //           v
    // aaaaaaaaaa
    //     ^
    //     data

    buf = (const char*)data;
    while (size > 0) {
        size_t n = fwrite(buf, 1, size, f);
        if (ferror(f)) {
            fprintf(stderr, "ERROR: Could not write into file %s: %s\n", path, strerror(errno));
            goto fail;
        }
        size -= n;
        buf  += n;
    }

    fclose(f);
    return true;
fail:
    if (f) fclose(f);
    return false;
}

struct {
    const char **items;
    size_t count;
    size_t capacity;
} labels = {0};

const char *intern_label(const char *label)
{
    for (size_t i = 0; i < labels.count; ++i) {
        if (strcmp(labels.items[i], label) == 0) {
            return labels.items[i];
        }
    }
    char *result = copy_string(label);
    da_append(&labels, result);
    return result;
}

typedef enum {
    // Free variable that still has a printed name.
    EXPR_FREE_NAME,
    // Free de Bruijn index. It is relative to the outermost context, so it is
    // not affected by the binders of the term it appears in.
    EXPR_FREE_INDEX,
    // Variable bound by one of the enclosing lambdas. The index counts the
    // binders between the occurrence and its binder.
    EXPR_BOUND,
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
        const char *free_name;
        size_t free_index;
        size_t bound;
        const char *mag;
        struct {
            // NULL means an anonymous binder written as `\.`
            const char *param;
            Expr_Index body;
        } fun;
        struct {
            Expr_Index lhs;
            Expr_Index rhs;
        } app;
    } as;
} Expr;

static struct {
    struct {
        Expr *items;
        size_t count;
        size_t capacity;
    } slots;

    struct {
        Expr_Index *items;
        size_t count;
        size_t capacity;
    } dead;

    struct {
        Expr_Index *items;
        size_t count;
        size_t capacity;
    } gens[2];

    size_t gen_cur;
} GC = {0};

#define expr_slot(index) (                            \
    GC.slots.items[                                   \
        (assert((index).unwrap < GC.slots.count),     \
         assert(GC.slots.items[(index).unwrap].live), \
         (index).unwrap)])

#define expr_slot_unsafe(index) GC.slots.items[(index).unwrap]


Expr_Index alloc_expr(void)
{
    Expr_Index result;
    if (GC.dead.count > 0) {
        result = GC.dead.items[--GC.dead.count];
    } else {
        result.unwrap = GC.slots.count;
        Expr expr = {0};
        da_append(&GC.slots, expr);
    }
    assert(!expr_slot_unsafe(result).live);
    expr_slot_unsafe(result).live = true;
    da_append(&GC.gens[GC.gen_cur], result);
    return result;
}

void free_expr(Expr_Index expr)
{
    expr_slot(expr).live = false;
    da_append(&GC.dead, expr);
}

Expr_Index free_var(const char *label)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_FREE_NAME;
    expr_slot(expr).as.free_name = intern_label(label);
    return expr;
}

Expr_Index free_index(size_t index)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_FREE_INDEX;
    expr_slot(expr).as.free_index = index;
    return expr;
}

Expr_Index bound_var(size_t index)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_BOUND;
    expr_slot(expr).as.bound = index;
    return expr;
}

Expr_Index magic(const char *label)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_MAG;
    expr_slot(expr).as.mag = intern_label(label);
    return expr;
}

Expr_Index fun(const char *param, Expr_Index body)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_FUN;
    expr_slot(expr).as.fun.param = param;
    expr_slot(expr).as.fun.body = body;
    return expr;
}

Expr_Index app(Expr_Index lhs, Expr_Index rhs)
{
    Expr_Index expr = alloc_expr();
    expr_slot(expr).kind = EXPR_APP;
    expr_slot(expr).as.app.lhs = lhs;
    expr_slot(expr).as.app.rhs = rhs;
    return expr;
}

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Binder_Stack;

typedef enum {
    // Print bound variables by the name of their binder when possible.
    DEBRUIJN_OFF,
    // Print every bound variable as an index, but keep binder names.
    DEBRUIJN_ON,
    // Print every binder as `\.` and every variable as an index, so the
    // output contains no names at all.
    DEBRUIJN_FULL,
} Debruijn_Mode;

// Selected by the `:debruijn` command.
static Debruijn_Mode debruijn_mode = DEBRUIJN_OFF;

const char *debruijn_mode_name(Debruijn_Mode mode)
{
    switch (mode) {
    case DEBRUIJN_OFF:  return "OFF";
    case DEBRUIJN_ON:   return "ON";
    case DEBRUIJN_FULL: return "FULL";
    default: UNREACHABLE("Debruijn_Mode");
    }
}

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Free_Name_Stack;

// Free variables have no binder that could give them an index. In FULL mode
// they are numbered by first appearance instead, as if the free variables
// formed an outer context. This is what makes the output completely nameless.
size_t free_name_index(Free_Name_Stack *stack, const char *label)
{
    for (size_t i = 0; i < stack->count; ++i) {
        if (stack->items[i] == label) return i;
    }
    da_append(stack, label);
    return stack->count - 1;
}

bool has_free_name(Expr_Index expr, const char *label)
{
    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
        return expr_slot(expr).as.free_name == label;
    case EXPR_FREE_INDEX:
    case EXPR_BOUND:
    case EXPR_MAG:
        return false;
    case EXPR_FUN:
        return has_free_name(expr_slot(expr).as.fun.body, label);
    case EXPR_APP:
        return has_free_name(expr_slot(expr).as.app.lhs, label) ||
               has_free_name(expr_slot(expr).as.app.rhs, label);
    default: UNREACHABLE("Expr_Kind");
    }
}

// Picks the name under which a binder is printed. If a free variable with the
// same name occurs in the body, printing the original name would capture it,
// so a fresh name is derived from it.
const char *display_binder_name(const char *param, Expr_Index body)
{
    if (param == NULL) return NULL;
    if (!has_free_name(body, param)) return param;

    static String_Builder sb = {0};
    for (size_t i = 1;; ++i) {
        sb.count = 0;
        sb_appendf(&sb, "%s_%zu", param, i);
        sb_append_null(&sb);
        const char *candidate = intern_label(sb.items);
        if (!has_free_name(body, candidate)) return candidate;
    }
}

// Whether the binder at `index` is hidden by a closer binder with the same
// display name. Such variables are printed as indices to avoid ambiguity.
bool bound_name_is_shadowed(Binder_Stack *stack, size_t index)
{
    assert(index < stack->count);
    const char *label = stack->items[stack->count - 1 - index];
    if (label == NULL) return true;
    for (size_t i = stack->count - index; i < stack->count; ++i) {
        if (stack->items[i] == label) return true;
    }
    return false;
}

void expr_display_scope(Expr_Index expr, String_Builder *sb, Binder_Stack *stack,
                        Free_Name_Stack *free_names)
{
    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
        if (debruijn_mode == DEBRUIJN_FULL) {
            sb_appendf(sb, "%zu",
                       stack->count + free_name_index(free_names, expr_slot(expr).as.free_name));
        } else {
            sb_appendf(sb, "%s", expr_slot(expr).as.free_name);
        }
        break;
    case EXPR_FREE_INDEX:
        sb_appendf(sb, "%zu", stack->count + expr_slot(expr).as.free_index);
        break;
    case EXPR_BOUND: {
        size_t index = expr_slot(expr).as.bound;
        if (debruijn_mode == DEBRUIJN_OFF && !bound_name_is_shadowed(stack, index)) {
            sb_appendf(sb, "%s", stack->items[stack->count - 1 - index]);
        } else {
            sb_appendf(sb, "%zu", index);
        }
    } break;
    case EXPR_FUN: {
        const char *param = expr_slot(expr).as.fun.param;
        const char *name = debruijn_mode == DEBRUIJN_FULL
            ? NULL
            : display_binder_name(param, expr_slot(expr).as.fun.body);
        sb_appendf(sb, "\\");
        if (name) {
            sb_appendf(sb, "%s.", name);
        } else {
            sb_appendf(sb, ".");
        }
        da_append(stack, name);
        expr_display_scope(expr_slot(expr).as.fun.body, sb, stack, free_names);
        stack->count -= 1;
    } break;
    case EXPR_APP: {
        Expr_Index lhs = expr_slot(expr).as.app.lhs;
        bool lhs_paren = expr_slot(lhs).kind == EXPR_FUN;
        if (lhs_paren) sb_appendf(sb, "(");
        expr_display_scope(lhs, sb, stack, free_names);
        if (lhs_paren) sb_appendf(sb, ")");

        sb_appendf(sb, " ");

        Expr_Index rhs = expr_slot(expr).as.app.rhs;
        bool rhs_paren = expr_slot(rhs).kind != EXPR_BOUND &&
                         expr_slot(rhs).kind != EXPR_FREE_NAME &&
                         expr_slot(rhs).kind != EXPR_FREE_INDEX &&
                         expr_slot(rhs).kind != EXPR_MAG;
        if (rhs_paren) sb_appendf(sb, "(");
        expr_display_scope(rhs, sb, stack, free_names);
        if (rhs_paren) sb_appendf(sb, ")");
    } break;
    case EXPR_MAG:
        sb_appendf(sb, "#%s", expr_slot(expr).as.mag);
        break;
    default: UNREACHABLE("Expr_Kind");
    }
}

void expr_display(Expr_Index expr, String_Builder *sb)
{
    static Binder_Stack stack = {0};
    static Free_Name_Stack free_names = {0};
    stack.count = 0;
    free_names.count = 0;
    expr_display_scope(expr, sb, &stack, &free_names);
}

void dump_expr_ast_scope(Expr_Index expr, Binder_Stack *binders,
                         Free_Name_Stack *free_names)
{
    static struct {
        bool *items;
        size_t count;
        size_t capacity;
    } stack = {0};

    for (size_t i = 0; i < stack.count; ++i) {
        if (i + 1 == stack.count) {
            printf("+--");
        } else {
            if (stack.items[i]) {
                printf("|  ");
            } else {
                printf("   ");
            }
        }
    }

    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
        if (debruijn_mode == DEBRUIJN_FULL) {
            printf("[VAR] %zu\n",
                   binders->count + free_name_index(free_names, expr_slot(expr).as.free_name));
        } else {
            printf("[VAR] %s\n", expr_slot(expr).as.free_name);
        }
        break;
    case EXPR_FREE_INDEX:
        printf("[VAR] %zu\n", binders->count + expr_slot(expr).as.free_index);
        break;
    case EXPR_BOUND: {
        size_t index = expr_slot(expr).as.bound;
        if (debruijn_mode == DEBRUIJN_OFF && !bound_name_is_shadowed(binders, index)) {
            printf("[VAR] %s\n", binders->items[binders->count - 1 - index]);
        } else {
            printf("[VAR] %zu\n", index);
        }
    } break;
    case EXPR_FUN: {
        const char *param = expr_slot(expr).as.fun.param;
        const char *name = debruijn_mode == DEBRUIJN_FULL
            ? NULL
            : display_binder_name(param, expr_slot(expr).as.fun.body);
        if (name) {
            printf("[FUN] \\%s\n", name);
        } else {
            printf("[FUN] \\.\n");
        }
        da_append(binders, name);
        da_append(&stack, false); {
            dump_expr_ast_scope(expr_slot(expr).as.fun.body, binders, free_names);
        } stack.count -= 1;
        binders->count -= 1;
    } break;
    case EXPR_APP:
        printf("[APP]\n");
        da_append(&stack, true); {
            dump_expr_ast_scope(expr_slot(expr).as.app.lhs, binders, free_names);
        } stack.count -= 1;
        da_append(&stack, false); {
            dump_expr_ast_scope(expr_slot(expr).as.app.rhs, binders, free_names);
        } stack.count -= 1;
        break;
    case EXPR_MAG:
        printf("[MAG] #%s\n", expr_slot(expr).as.mag);
        break;
    default:
        UNREACHABLE("Expr_Kind");
    }
}

void dump_expr_ast(Expr_Index expr)
{
    static Binder_Stack binders = {0};
    static Free_Name_Stack free_names = {0};
    binders.count = 0;
    free_names.count = 0;
    dump_expr_ast_scope(expr, &binders, &free_names);
}

void trace_expr(Expr_Index expr)
{
    static String_Builder sb = {0};
    sb.count = 0;
    expr_display(expr, &sb);
    sb_append_null(&sb);
    printf("%s", sb.items);
}

// Shifts bound variables of `expr` by `delta`. Variables under `cutoff` or
// more binders are considered local to the term and are left alone.
Expr_Index expr_shift(Expr_Index expr, size_t delta, size_t cutoff)
{
    if (delta == 0) return expr;
    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
    case EXPR_FREE_INDEX:
    case EXPR_MAG:
        return expr;
    case EXPR_BOUND: {
        size_t index = expr_slot(expr).as.bound;
        if (index >= cutoff) return bound_var(index + delta);
        return expr;
    }
    case EXPR_FUN: {
        const char *param = expr_slot(expr).as.fun.param;
        return fun(param, expr_shift(expr_slot(expr).as.fun.body, delta, cutoff + 1));
    }
    case EXPR_APP: {
        Expr_Index lhs = expr_slot(expr).as.app.lhs;
        Expr_Index rhs = expr_slot(expr).as.app.rhs;
        return app(expr_shift(lhs, delta, cutoff), expr_shift(rhs, delta, cutoff));
    }
    default: UNREACHABLE("Expr_Kind");
    }
}

// Replaces the variable bound by the lambda being reduced in `body` with
// `arg`. `depth` counts the binders crossed inside `body` on the way to the
// replaced variable.
Expr_Index beta_subst(Expr_Index body, Expr_Index arg, size_t depth)
{
    switch (expr_slot(body).kind) {
    case EXPR_FREE_NAME:
    case EXPR_FREE_INDEX:
    case EXPR_MAG:
        return body;
    case EXPR_BOUND: {
        size_t index = expr_slot(body).as.bound;
        if (index == depth) return expr_shift(arg, depth, 0);
        if (index > depth) return bound_var(index - 1);
        return body;
    }
    case EXPR_FUN: {
        const char *param = expr_slot(body).as.fun.param;
        return fun(param, beta_subst(expr_slot(body).as.fun.body, arg, depth + 1));
    }
    case EXPR_APP: {
        Expr_Index lhs = expr_slot(body).as.app.lhs;
        Expr_Index rhs = expr_slot(body).as.app.rhs;
        return app(
            beta_subst(lhs, arg, depth),
            beta_subst(rhs, arg, depth));
    }
    default: UNREACHABLE("Expr_Kind");
    }
}

// Replaces free variables named `name` with `replacement`. `depth` is the
// number of binders crossed, which is how much the free bound variables of
// the replacement need to be shifted.
Expr_Index subst_free_name(Expr_Index expr, const char *name, Expr_Index replacement, size_t depth)
{
    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
        if (expr_slot(expr).as.free_name == name) {
            return expr_shift(replacement, depth, 0);
        }
        return expr;
    case EXPR_FREE_INDEX:
    case EXPR_BOUND:
    case EXPR_MAG:
        return expr;
    case EXPR_FUN: {
        const char *param = expr_slot(expr).as.fun.param;
        return fun(param, subst_free_name(expr_slot(expr).as.fun.body, name, replacement, depth + 1));
    }
    case EXPR_APP: {
        Expr_Index lhs = expr_slot(expr).as.app.lhs;
        Expr_Index rhs = expr_slot(expr).as.app.rhs;
        return app(
            subst_free_name(lhs, name, replacement, depth),
            subst_free_name(rhs, name, replacement, depth));
    }
    default: UNREACHABLE("Expr_Kind");
    }
}

bool eval1(Expr_Index expr, Expr_Index *expr1)
{
    switch (expr_slot(expr).kind) {
    case EXPR_FREE_NAME:
    case EXPR_FREE_INDEX:
    case EXPR_BOUND:
    case EXPR_MAG:
        *expr1 = expr;
        return true;
    case EXPR_FUN: {
        Expr_Index body;
        if (!eval1(expr_slot(expr).as.fun.body, &body)) return false;
        if (body.unwrap != expr_slot(expr).as.fun.body.unwrap) {
            *expr1 = fun(expr_slot(expr).as.fun.param, body);
        } else {
            *expr1 = expr;
        }
        return true;
    }
    case EXPR_APP: {
        Expr_Index lhs = expr_slot(expr).as.app.lhs;
        Expr_Index rhs = expr_slot(expr).as.app.rhs;

        if (expr_slot(lhs).kind == EXPR_FUN) {
            *expr1 = beta_subst(expr_slot(lhs).as.fun.body, rhs, 0);
            return true;
        } else if (expr_slot(lhs).kind == EXPR_MAG) {
            if (expr_slot(lhs).as.mag == intern_label("trace")) {
                Expr_Index new_rhs;
                if (!eval1(rhs, &new_rhs)) return false;
                if (new_rhs.unwrap == rhs.unwrap) {
                    printf("TRACE: ");
                    trace_expr(rhs);
                    printf("\n");
                    *expr1 = rhs;
                } else {
                    *expr1 = app(lhs, new_rhs);
                }
                return true;
            } else if (expr_slot(lhs).as.mag == intern_label("void")) {
                Expr_Index new_rhs;
                if (!eval1(rhs, &new_rhs)) return false;
                if (new_rhs.unwrap == rhs.unwrap) {
                    *expr1 = lhs;
                } else {
                    *expr1 = app(lhs, new_rhs);
                }
                return true;
            } else {
                printf("ERROR: unknown magic #%s\n", expr_slot(lhs).as.mag);
                return false;
            }
        }

        Expr_Index new_lhs;
        if (!eval1(lhs, &new_lhs)) return false;
        if (lhs.unwrap != new_lhs.unwrap) {
            *expr1 = app(new_lhs, rhs);
            return true;
        }

        Expr_Index new_rhs;
        if (!eval1(rhs, &new_rhs)) return false;
        if (rhs.unwrap != new_rhs.unwrap) {
            *expr1 = app(lhs, new_rhs);
            return true;
        }

        *expr1 = expr;
        return true;
    }
    default: UNREACHABLE("Expr_Kind");
    }
}

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
    TOKEN_INDEX,
    TOKEN_MAGIC,
} Token_Kind;

const char *token_kind_display(Token_Kind kind)
{
    switch (kind) {
    case TOKEN_INVALID:   return "TOKEN_INVALID";
    case TOKEN_END:       return "TOKEN_END";
    case TOKEN_OPAREN:    return "TOKEN_OPAREN";
    case TOKEN_CPAREN:    return "TOKEN_CPAREN";
    case TOKEN_LAMBDA:    return "TOKEN_LAMBDA";
    case TOKEN_DOT:       return "TOKEN_DOT";
    case TOKEN_COLON:     return "TOKEN_COLON";
    case TOKEN_SEMICOLON: return "TOKEN_SEMICOLON";
    case TOKEN_EQUALS:    return "TOKEN_EQUALS";
    case TOKEN_NAME:      return "TOKEN_NAME";
    case TOKEN_INDEX:     return "TOKEN_INDEX";
    case TOKEN_MAGIC:     return "TOKEN_MAGIC";
    default: UNREACHABLE("Token_Kind");
    }
}

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
    size_t index;
    size_t row, col;
} Lexer;

void lexer_init(Lexer *l, const char *content, size_t count, const char *file_path)
{
    l->content = content;
    l->count = count;
    l->file_path = file_path;
    memset(&l->cur, 0, sizeof(l->cur));
}

void lexer_print_loc(Lexer *l, FILE *stream)
{
    if (l->file_path) fprintf(stream, "%s:", l->file_path);
    fprintf(stream, "%zu:%zu: ", l->row, l->col);
}

char lexer_curr_char(Lexer *l)
{
    if (l->cur.pos >= l->count) return 0;
    return l->content[l->cur.pos];
}

char lexer_next_char(Lexer *l)
{
    if (l->cur.pos >= l->count) return 0;
    char x = l->content[l->cur.pos++];
    if (x == '\n') {
        l->cur.row += 1;
        l->cur.bol = l->cur.pos;
    }
    return x;
}

bool is_ascii_space(int x)
{
    return x == ' ' || x == '\t' || x == '\n' || x == '\r' || x == '\v' || x == '\f';
}

// Decodes one UTF-8 code point from `s`. Returns the number of bytes consumed,
// or 0 for an invalid or incomplete sequence. Rejects overlong forms, UTF-16
// surrogates, and code points above U+10FFFF.
size_t decode_utf8(const char *s, size_t n, uint32_t *cp)
{
    const unsigned char *bytes = (const unsigned char *) s;
    if (n == 0) return 0;

    unsigned char b0 = bytes[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }

    size_t len;
    uint32_t value;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
        value = b0 & 0x1F;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        value = b0 & 0x0F;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        value = b0 & 0x07;
    } else {
        return 0;
    }

    if (n < len) return 0;
    for (size_t i = 1; i < len; ++i) {
        if ((bytes[i] & 0xC0) != 0x80) return 0;
        value = (value << 6) | (bytes[i] & 0x3F);
    }

    if ((len == 3 && value < 0x800) ||
        (len == 4 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) ||
        value > 0x10FFFF) {
        return 0;
    }

    *cp = value;
    return len;
}

// Counts characters in a byte slice, treating every invalid byte as one
// character. Used to report columns in characters rather than bytes.
size_t count_chars(const char *s, size_t n)
{
    size_t count = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t cp = 0;
        size_t len = decode_utf8(s + i, n - i, &cp);
        i += len > 0 ? len : 1;
        ++count;
    }
    return count;
}

// Selects a locale for iswalnum(), which classifies non-ASCII name
// characters. The lexer decodes UTF-8 itself; the locale is only used for
// classification. Prefer a UTF-8 locale and fall back to the system one.
void init_name_locale(void)
{
    static const char *const candidates[] = {
        "C.UTF-8", ".UTF-8", "en_US.UTF-8", "UTF-8", ".utf8",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (setlocale(LC_CTYPE, candidates[i])) return;
    }
    setlocale(LC_CTYPE, "");
}

bool is_ascii_alnum(uint32_t cp)
{
    return (cp >= '0' && cp <= '9') ||
           (cp >= 'a' && cp <= 'z') ||
           (cp >= 'A' && cp <= 'Z');
}

// Whether `cp` is a digit. Digits cannot start a name: they are de Bruijn
// indices instead.
bool is_digit_cp(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') return true;
    if (cp < 0x80) return false;
    if (sizeof(wint_t) <= 2 && cp > 0xFFFF) return false;
    return iswdigit((wint_t) cp) != 0;
}

// Whether `cp` may appear in a name. ASCII letters and digits are always
// accepted; non-ASCII letters and digits are accepted when the C library's
// locale knows them (see init_name_locale()). `_`, `?`, `-`, and `'` are
// always accepted, while `λ` is reserved for the lambda operator.
bool is_name_cp(uint32_t cp)
{
    if (cp == 0x03BB) return false;
    if (cp < 0x80) {
        return is_ascii_alnum(cp) || cp == '_' || cp == '?' || cp == '-' || cp == '\'';
    }
    if (sizeof(wint_t) <= 2 && cp > 0xFFFF) return false;
    return iswalnum((wint_t) cp) != 0;
}

void lexer_read_name_chars(Lexer *l)
{
    for (;;) {
        uint32_t cp = 0;
        size_t len = decode_utf8(l->content + l->cur.pos, l->count - l->cur.pos, &cp);
        if (len == 0 || !is_name_cp(cp)) break;
        for (size_t i = 0; i < len; ++i) {
            da_append(&l->string, lexer_next_char(l));
        }
    }
}

void lexer_trim_left(Lexer *l)
{
    while (is_ascii_space(lexer_curr_char(l))) {
        lexer_next_char(l);
    }
}

bool lexer_starts_with(Lexer *l, const char *prefix)
{
    size_t pos = l->cur.pos;
    while (pos < l->count && *prefix != '\0' && *prefix == l->content[pos]) {
        pos++;
        prefix++;
    }
    return *prefix == '\0';
}

void lexer_drop_line(Lexer *l)
{
    while (l->cur.pos < l->count && lexer_next_char(l) != '\n') {}
}

bool lexer_next(Lexer *l)
{
    for (;;) {
        lexer_trim_left(l);
        if (lexer_starts_with(l, "//")) lexer_drop_line(l);
        else break;
    }

    l->row = l->cur.row + 1;
    l->col = 1 + count_chars(l->content + l->cur.bol, l->cur.pos - l->cur.bol);

    if (l->cur.pos >= l->count) {
        l->token = TOKEN_END;
        return true;
    }

    size_t start = l->cur.pos;
    uint32_t cp = 0;
    size_t len = decode_utf8(l->content + start, l->count - start, &cp);
    if (len == 0) {
        // Invalid UTF-8 is reported one byte at a time.
        char x = lexer_next_char(l);
        l->token = TOKEN_INVALID;
        lexer_print_loc(l, stderr);
        fprintf(stderr, "ERROR: Unknown token starts with `%c`\n", x);
        return false;
    }

    // A NUL byte is treated as the end of input.
    if (cp == 0) {
        for (size_t i = 0; i < len; ++i) lexer_next_char(l);
        l->token = TOKEN_END;
        return true;
    }

    // U+03BB GREEK SMALL LETTER LAMDA as an alternative spelling of `\`.
    if (cp == 0x03BB) {
        for (size_t i = 0; i < len; ++i) lexer_next_char(l);
        l->token = TOKEN_LAMBDA;
        return true;
    }

    if (cp < 0x80) {
        switch ((char) cp) {
        case '(':  lexer_next_char(l); l->token = TOKEN_OPAREN;    return true;
        case ')':  lexer_next_char(l); l->token = TOKEN_CPAREN;    return true;
        case '\\': lexer_next_char(l); l->token = TOKEN_LAMBDA;    return true;
        case '.':  lexer_next_char(l); l->token = TOKEN_DOT;       return true;
        case ':':  lexer_next_char(l); l->token = TOKEN_COLON;     return true;
        case ';':  lexer_next_char(l); l->token = TOKEN_SEMICOLON; return true;
        case '=':  lexer_next_char(l); l->token = TOKEN_EQUALS;     return true;
        case '#':
            lexer_next_char(l);
            l->token = TOKEN_MAGIC;
            l->string.count = 0;
            lexer_read_name_chars(l);
            sb_append_null(&l->string);
            return true;
        }
    }

    // A run of ASCII digits is a de Bruijn index. Names cannot start with a
    // digit, so `1abc` is rejected instead of being split into two tokens.
    if (cp >= '0' && cp <= '9') {
        size_t index = 0;
        bool overflow = false;
        while (lexer_curr_char(l) >= '0' && lexer_curr_char(l) <= '9') {
            size_t digit = (size_t)(lexer_curr_char(l) - '0');
            if (index > (SIZE_MAX - digit) / 10) {
                overflow = true;
            } else {
                index = index * 10 + digit;
            }
            lexer_next_char(l);
        }

        uint32_t next_cp = 0;
        size_t next_len = decode_utf8(l->content + l->cur.pos, l->count - l->cur.pos, &next_cp);
        if (next_len > 0 && is_name_cp(next_cp)) {
            l->token = TOKEN_INVALID;
            lexer_print_loc(l, stderr);
            fprintf(stderr, "ERROR: A name cannot start with a digit\n");
            return false;
        }

        if (overflow) {
            l->token = TOKEN_INVALID;
            lexer_print_loc(l, stderr);
            fprintf(stderr, "ERROR: De Bruijn index is too large\n");
            return false;
        }

        l->token = TOKEN_INDEX;
        l->index = index;
        return true;
    }

    if (is_name_cp(cp) && !is_digit_cp(cp)) {
        l->token = TOKEN_NAME;
        l->string.count = 0;
        for (size_t i = 0; i < len; ++i) {
            da_append(&l->string, lexer_next_char(l));
        }
        lexer_read_name_chars(l);
        sb_append_null(&l->string);
        return true;
    }

    if (is_name_cp(cp) && is_digit_cp(cp)) {
        for (size_t i = 0; i < len; ++i) lexer_next_char(l);
        l->token = TOKEN_INVALID;
        lexer_print_loc(l, stderr);
        fprintf(stderr, "ERROR: A name cannot start with a digit `%.*s`\n", (int) len, l->content + start);
        return false;
    }

    for (size_t i = 0; i < len; ++i) lexer_next_char(l);
    l->token = TOKEN_INVALID;
    lexer_print_loc(l, stderr);
    fprintf(stderr, "ERROR: Unknown token starts with `%.*s`\n", (int) len, l->content + start);
    return false;
}

bool lexer_peek(Lexer *l)
{
    Cur cur = l->cur;
    bool result = lexer_next(l);
    l->cur = cur;
    return result;
}

void report_unexpected(Lexer *l, Token_Kind expected)
{
    lexer_print_loc(l, stderr);
    fprintf(stderr, "ERROR: Unexpected token %s. Expected %s instead.\n", token_kind_display(l->token), token_kind_display(expected));
}

bool lexer_expect(Lexer *l, Token_Kind expected)
{
    if (!lexer_next(l)) return false;
    if (l->token != expected) {
        report_unexpected(l, expected);
        return false;
    }
    return true;
}

bool parse_expr_scope(Lexer *l, Binder_Stack *scope, Expr_Index *expr);

bool parse_fun_scope(Lexer *l, Binder_Stack *scope, Expr_Index *expr)
{
    const char *param = NULL;
    if (!lexer_next(l)) return false;
    if (l->token == TOKEN_DOT) {
        // `\.body` is an anonymous binder that can only be referred to with
        // de Bruijn indices.
    } else if (l->token == TOKEN_NAME) {
        param = intern_label(l->string.items);
        if (!lexer_expect(l, TOKEN_DOT)) return false;
    } else {
        lexer_print_loc(l, stderr);
        fprintf(stderr, "ERROR: Unexpected token %s. Expected a parameter name or `.` instead.\n",
                token_kind_display(l->token));
        return false;
    }

    // NOTE: `\x.y.body` means `\x.\y.body`
    bool shorthand = false;
    {
        Cur cur = l->cur;
        if (!lexer_next(l)) return false;
        if (l->token == TOKEN_NAME) {
            if (!lexer_next(l)) return false;
            shorthand = l->token == TOKEN_DOT;
        }
        l->cur = cur;
    }

    da_append(scope, param);
    Expr_Index body;
    bool ok = shorthand
        ? parse_fun_scope(l, scope, &body)
        : parse_expr_scope(l, scope, &body);
    scope->count -= 1;
    if (!ok) return false;

    *expr = fun(param, body);
    return true;
}

bool parse_primary_scope(Lexer *l, Binder_Stack *scope, Expr_Index *expr)
{
    if (!lexer_next(l)) return false;
    switch (l->token) {
    case TOKEN_OPAREN: {
        if (!parse_expr_scope(l, scope, expr)) return false;
        if (!lexer_expect(l, TOKEN_CPAREN)) return false;
        return true;
    }
    case TOKEN_LAMBDA: return parse_fun_scope(l, scope, expr);
    case TOKEN_MAGIC:
        *expr = magic(l->string.items);
        return true;
    case TOKEN_INDEX: {
        size_t index = l->index;
        if (index < scope->count) {
            *expr = bound_var(index);
        } else {
            *expr = free_index(index - scope->count);
        }
        return true;
    }
    case TOKEN_NAME: {
        const char *label = intern_label(l->string.items);
        for (size_t i = scope->count; i > 0; --i) {
            if (scope->items[i - 1] == label) {
                *expr = bound_var(scope->count - i);
                return true;
            }
        }
        *expr = free_var(label);
        return true;
    }
    default:
        lexer_print_loc(l, stderr);
        fprintf(stderr, "ERROR: Unexpected token %s. Expected a primary expression instead.\n", token_kind_display(l->token));
        return false;
    }
}

bool parse_expr_scope(Lexer *l, Binder_Stack *scope, Expr_Index *expr)
{
    if (!parse_primary_scope(l, scope, expr)) return false;

    if (!lexer_peek(l)) return false;
    while (
        l->token != TOKEN_CPAREN &&
        l->token != TOKEN_END    &&
        l->token != TOKEN_SEMICOLON
    ) {
        Expr_Index rhs;
        if (!parse_primary_scope(l, scope, &rhs)) return false;
        *expr = app(*expr, rhs);
        if (!lexer_peek(l)) return false;
    }
    return true;
}

bool parse_expr(Lexer *l, Expr_Index *expr)
{
    static Binder_Stack scope = {0};
    scope.count = 0;
    return parse_expr_scope(l, &scope, expr);
}

bool parse_binding_body(Lexer *l, Expr_Index *body)
{
    // NOTE: the parameters of a binding are just syntactic sugar for nested lambdas.
    // `f x y = body;` is equivalent to `f = \x.y.body;`
    static Binder_Stack scope = {0};
    scope.count = 0;

    for (;;) {
        if (!lexer_next(l)) return false;
        if (l->token != TOKEN_NAME) break;
        da_append(&scope, intern_label(l->string.items));
    }

    if (l->token != TOKEN_EQUALS) {
        report_unexpected(l, TOKEN_EQUALS);
        return false;
    }

    if (!parse_expr_scope(l, &scope, body)) return false;

    for (size_t i = scope.count; i > 0; --i) {
        *body = fun(scope.items[i - 1], *body);
    }
    scope.count = 0;
    return true;
}

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

bool command(Commands *commands, const char *input, const char *name, const char *signature, const char *description)
{
    Command command = {
        .name        = name,
        .signature   = signature,
        .description = description,
    };
    da_append(commands, command);
    while (*input && *name && *input == *name) {
        input++;
        name++;
    }
    return *input == '\0';
}

void print_available_commands(Commands *commands)
{
    printf("Available commands:\n");
    int max_name_width = 0;
    int max_sig_width = 0;
    for (size_t i = 0; i < commands->count; ++i) {
        Command command = commands->items[i];
        int name_width  = strlen(command.name);
        int sig_width   = strlen(command.signature);
        if (name_width > max_name_width) max_name_width = name_width;
        if (sig_width  > max_sig_width)  max_sig_width  = sig_width;
    }
    for (size_t i = 0; i < commands->count; ++i) {
        Command command = commands->items[i];
        printf("  :%-*s %-*s - %s\n",
               max_name_width, command.name,
               max_sig_width,  command.signature,
               command.description);
    }
}

void gc_mark(Expr_Index root)
{
    if (expr_slot(root).visited) return;
    expr_slot(root).visited = true;
    switch (expr_slot(root).kind) {
    case EXPR_MAG:
    case EXPR_FREE_NAME:
    case EXPR_FREE_INDEX:
    case EXPR_BOUND:
        break;
    case EXPR_FUN:
        gc_mark(expr_slot(root).as.fun.body);
        break;
    case EXPR_APP:
        gc_mark(expr_slot(root).as.app.lhs);
        gc_mark(expr_slot(root).as.app.rhs);
        break;
    default: UNREACHABLE("Expr_Kind");
    }
}

typedef struct {
    const char *name;
    Expr_Index body;
} Binding;

typedef struct {
    Binding *items;
    size_t count;
    size_t capacity;
} Bindings;

void create_binding(Bindings *bindings, const char *name, Expr_Index body)
{
    for (size_t i = 0; i < bindings->count; ++i) {
        if (bindings->items[i].name == name) {
            bindings->items[i].body = body;
            printf("Updated binding %s\n", name);
            return;
        }
    }
    Binding binding = {
        .name = name,
        .body = body,
    };
    da_append(bindings, binding);
    printf("Created binding %s\n", name);
}

bool create_bindings_from_file(const char *file_path, Bindings *bindings)
{
    static String_Builder sb = {0};
    static Lexer l = {0};

    sb.count = 0;
    if (!read_entire_file(file_path, &sb)) return false;

    lexer_init(&l, sb.items, sb.count, file_path);

    if (!lexer_peek(&l)) return false;
    while (l.token != TOKEN_END) {
        if (!lexer_expect(&l, TOKEN_NAME)) return false;
        const char *name = intern_label(l.string.items);
        Expr_Index body;
        if (!parse_binding_body(&l, &body)) return false;
        if (!lexer_expect(&l, TOKEN_SEMICOLON)) return false;
        create_binding(bindings, name, body);
        if (!lexer_peek(&l)) return false;
    }
    return true;
}

void gc(Expr_Index root, Bindings bindings)
{
    for (size_t i = 0; i < GC.gens[GC.gen_cur].count; ++i) {
        Expr_Index expr = GC.gens[GC.gen_cur].items[i];
        expr_slot(expr).visited = false;
    }

    gc_mark(root);
    for (size_t i = 0; i < bindings.count; ++i) {
        gc_mark(bindings.items[i].body);
    }

    size_t next = 1 - GC.gen_cur;
    GC.gens[next].count = 0;
    for (size_t i = 0; i < GC.gens[GC.gen_cur].count; ++i) {
        Expr_Index expr = GC.gens[GC.gen_cur].items[i];
        if (expr_slot(expr).visited) {
            da_append(&GC.gens[next], expr);
        } else {
            free_expr(expr);
        }
    }
    GC.gen_cur = next;
}

static volatile sig_atomic_t ctrl_c = 0;
void ctrl_c_handler(int signum)
{
    UNUSED(signum);
    ctrl_c = 1;
}

void replace_active_file_path_from_lexer_if_not_empty(Lexer l, char **active_file_path)
{
    const char *path_data = &l.content[l.cur.pos];
    size_t path_count = l.count - l.cur.pos;
    while (path_count > 0 && is_ascii_space((unsigned char) *path_data)) {
        path_data++;
        path_count--;
    }
    while (path_count > 0 && is_ascii_space((unsigned char) path_data[path_count - 1])) {
        path_count--;
    }

    if (path_count > 0) {
        free(*active_file_path);
        *active_file_path = copy_string_sized(path_data, path_count);
    }
}

int main(int argc, char **argv)
{
    static char buffer[1024];
    static Commands commands = {0};
    static Bindings bindings = {0};
    static Lexer l = {0};

    init_name_locale();

#ifndef _WIN32
    // TODO(20251221-171559): Handle ctrl+c on Windows
    //   signal() seem to be a standard thing https://en.cppreference.com/w/c/program/signal.html
    //   Yet Linux man pages say it's not portable. We need to start that out.
    struct sigaction act = {0};
    act.sa_handler = ctrl_c_handler;
    sigaction(SIGINT, &act, NULL);
#endif // _WIN32

    const char *editor  = getenv("LAMB_EDITOR");
    if (!editor) editor = getenv("EDITOR");
    if (!editor) editor = "vi";

    // NOTE: `active_file_path` is always located on the heap. If you need to replace it, first free() it
    // and then copy_string() it.
    char *active_file_path = NULL;

    if (argc == 2) {
        active_file_path = copy_string(argv[1]);
    } else if (argc > 2) {
        fprintf(stderr, "ERROR: only a single active file is support right now\n");
        return 1;
    }

    if (active_file_path) {
        create_bindings_from_file(active_file_path, &bindings);
    }

    printf(",---@>\n");
    printf(" W-W'\n");
    printf("Enter :help for more info\n");
    for (;;) {
again:
        printf("@> ");
        fflush(stdout);
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            if (feof(stdin)) goto quit;
            printf("\n");
            goto again;
        }
        const char *source = buffer;

        lexer_init(&l, source, strlen(source), NULL);

        if (!lexer_peek(&l)) goto again;
        if (l.token == TOKEN_END) goto again;
        if (l.token == TOKEN_COLON) {
            if (!lexer_next(&l)) goto again;
            if (!lexer_expect(&l, TOKEN_NAME)) goto again;
            commands.count = 0;
            if (command(&commands, l.string.items, "load", "[path]", "Load/reload bindings from a file.")) {
                replace_active_file_path_from_lexer_if_not_empty(l, &active_file_path);
                if (active_file_path == NULL) {
                    fprintf(stderr, "ERROR: No active file to reload from. Do `:load <path>`.\n");
                    goto again;
                }

                bindings.count = 0;
                create_bindings_from_file(active_file_path, &bindings);
                goto again;
            }
            if (command(&commands, l.string.items, "save", "[path]", "Save current bindings to a file.")) {
                replace_active_file_path_from_lexer_if_not_empty(l, &active_file_path);
                if (active_file_path == NULL) {
                    fprintf(stderr, "ERROR: No active file to save to. Do `:save <path>`.\n");
                    goto again;
                }

                static String_Builder sb = {0};
                sb.count = 0;
                for (size_t i = 0; i < bindings.count; ++i) {
                    sb_appendf(&sb, "%s = ", bindings.items[i].name);
                    expr_display(bindings.items[i].body, &sb);
                    sb_appendf(&sb, ";\n");
                }

                int exists = file_exists(active_file_path);
                if (exists < 0) goto again;
                if (exists) {
                    printf("WARNING! This command will override the formatting of %s. Really save? [N/y] ", active_file_path);
                    fflush(stdout);
                    if (!fgets(buffer, sizeof(buffer), stdin)) {
                        if (feof(stdin)) goto quit;
                        printf("\n");
                        goto again;
                    }
                    if (*buffer != 'y' && *buffer != 'Y') goto again;
                }

                if (!write_entire_file(active_file_path, sb.items, sb.count)) goto again;
                printf("Saved all the bindings to %s\n", active_file_path);
                goto again;
            }
            if (command(&commands, l.string.items, "edit", "[path]", "Edit current active file. Reload it on exit.")) {
#ifdef _WIN32
                fprintf(stderr, "TODO: editing files is not implemented on Windows yet! Sorry!\n");
#else
                replace_active_file_path_from_lexer_if_not_empty(l, &active_file_path);
                if (active_file_path == NULL) {
                    fprintf(stderr, "ERROR: No active file to edit. Do `:edit <path>`.\n");
                    goto again;
                }

                static Cmd cmd = {0};
                cmd.count = 0;
                da_append(&cmd, editor);
                da_append(&cmd, active_file_path);
                if (cmd_run(&cmd)) {
                    bindings.count = 0;
                    create_bindings_from_file(active_file_path, &bindings);
                }
#endif // _WIN32
                goto again;
            }
            if (command(&commands, l.string.items, "list", "[names...]", "list the bindings")) {
                static String_Builder sb = {0};
                static struct {
                    const char **items;
                    size_t count;
                    size_t capacity;
                } args = {0};

                args.count = 0;
                if (!lexer_next(&l)) goto again;
                while (l.token == TOKEN_NAME) {
                    da_append(&args, intern_label(l.string.items));
                    if (!lexer_next(&l)) goto again;
                }
                if (l.token != TOKEN_END) {
                    report_unexpected(&l, TOKEN_NAME);
                    goto again;
                }

                if (args.count == 0) {
                    for (size_t i = 0; i < bindings.count; ++i) {
                        sb.count = 0;
                        sb_appendf(&sb, "%s = ", bindings.items[i].name);
                        expr_display(bindings.items[i].body, &sb);
                        sb_appendf(&sb, ";");
                        sb_append_null(&sb);
                        printf("%s\n", sb.items);
                    }
                    goto again;
                }

                for (size_t j = 0; j < args.count; ++j) {
                    const char *label = args.items[j];
                    bool found = false;
                    for (size_t i = 0; !found && i < bindings.count; ++i) {
                        if (bindings.items[i].name == label) {
                            sb.count = 0;
                            sb_appendf(&sb, "%s = ", bindings.items[i].name);
                            expr_display(bindings.items[i].body, &sb);
                            sb_appendf(&sb, ";");
                            sb_append_null(&sb);
                            printf("%s\n", sb.items);
                            found = true;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "ERROR: binding %s does not exist\n", label);
                        goto again;
                    }
                }

                goto again;
            }
            if (command(&commands, l.string.items, "delete", "<name>", "delete a binding by name")) {
                if (!lexer_expect(&l, TOKEN_NAME)) goto again;
                const char *name = intern_label(l.string.items);
                for (size_t i = 0; i < bindings.count; ++i) {
                    if (bindings.items[i].name == name) {
                        da_delete_at(&bindings, i);
                        printf("Deleted binding %s\n", name);
                        goto again;
                    }
                }
                printf("ERROR: binding %s was not found\n", name);
                goto again;
            }
            if (command(&commands, l.string.items, "debug", "<expr>", "Step debug the evaluation of an expression")) {
                Expr_Index expr;
                if (!parse_expr(&l, &expr)) goto again;
                if (!lexer_expect(&l, TOKEN_END)) goto again;
                for (size_t i = bindings.count; i > 0; --i) {
                    expr = subst_free_name(expr, bindings.items[i-1].name, bindings.items[i-1].body, 0);
                }

                ctrl_c = 0;
                for (;;) {
                    if (ctrl_c) goto again; // TODO(20251220-002405)

                    printf("DEBUG: ");
                    trace_expr(expr);
                    printf("\n");

                    printf("-> ");

                    // TODO: get rid of the debug REPL. Just make it step through expressions by pressing Enter.
                    // Cancelling debug mode should be Ctrl+C which means we must sort it out on Windows.
                    // See 20251221-171559.
                    if (!fgets(buffer, sizeof(buffer), stdin)) {
                        if (feof(stdin)) goto quit;
                        printf("\n");
                        goto again;
                    }

                    lexer_init(&l, buffer, strlen(buffer), NULL);
                    if (!lexer_next(&l)) goto again;
                    if (l.token == TOKEN_NAME) {
                        if (strcmp(l.string.items, "quit") == 0) goto again;
                    }

                    gc(expr, bindings);

                    Expr_Index expr1;
                    if (!eval1(expr, &expr1)) goto again;
                    if (expr.unwrap == expr1.unwrap) break;
                    expr = expr1;
                }

                goto again;
            }
            if (command(&commands, l.string.items, "debruijn", "[on|off|full]", "Set variable output: names, indices, or fully nameless De Bruijn indices.")) {
                if (!lexer_next(&l)) goto again;
                if (l.token == TOKEN_END) {
                    debruijn_mode = debruijn_mode == DEBRUIJN_OFF
                        ? DEBRUIJN_ON
                        : DEBRUIJN_OFF;
                } else if (l.token == TOKEN_NAME && strcmp(l.string.items, "on") == 0) {
                    debruijn_mode = DEBRUIJN_ON;
                    if (!lexer_expect(&l, TOKEN_END)) goto again;
                } else if (l.token == TOKEN_NAME && strcmp(l.string.items, "off") == 0) {
                    debruijn_mode = DEBRUIJN_OFF;
                    if (!lexer_expect(&l, TOKEN_END)) goto again;
                } else if (l.token == TOKEN_NAME && strcmp(l.string.items, "full") == 0) {
                    debruijn_mode = DEBRUIJN_FULL;
                    if (!lexer_expect(&l, TOKEN_END)) goto again;
                } else {
                    lexer_print_loc(&l, stderr);
                    fprintf(stderr, "ERROR: Expected `on`, `off`, `full`, or no argument after :debruijn\n");
                    goto again;
                }
                printf("De Bruijn indices output: %s\n", debruijn_mode_name(debruijn_mode));
                goto again;
            }
            if (command(&commands, l.string.items, "ast", "<expr>", "print the AST of the expression")) {
                Expr_Index expr;
                if (!parse_expr(&l, &expr)) goto again;
                if (!lexer_expect(&l, TOKEN_END)) goto again;
                dump_expr_ast(expr);
                goto again;
            }
            if (command(&commands, l.string.items, "quit", "", "quit the REPL")) goto quit;
            if (command(&commands, l.string.items, "help", "", "print this help message")) {
                print_available_commands(&commands);
                goto again;
            }
            print_available_commands(&commands);
            printf("ERROR: unknown command `%s`\n", l.string.items);
            goto again;
        }

        // NOTE: `name arg1 arg2 = body` is a binding definition.
        bool is_binding = false;
        Cur cur = l.cur; {
            if (!lexer_next(&l)) goto again;
            if (l.token == TOKEN_NAME) {
                do {
                    if (!lexer_next(&l)) goto again;
                } while (l.token == TOKEN_NAME);
                is_binding = l.token == TOKEN_EQUALS;
            }
        } l.cur = cur;

        if (is_binding) {
            if (!lexer_expect(&l, TOKEN_NAME)) goto again;
            const char *name = intern_label(l.string.items);
            Expr_Index body;
            if (!parse_binding_body(&l, &body)) goto again;
            if (!lexer_expect(&l, TOKEN_END)) goto again;
            create_binding(&bindings, name, body);
            goto again;
        }

        Expr_Index expr;
        if (!parse_expr(&l, &expr)) goto again;
        if (!lexer_expect(&l, TOKEN_END)) goto again;
        for (size_t i = bindings.count; i > 0; --i) {
            expr = subst_free_name(expr, bindings.items[i-1].name, bindings.items[i-1].body, 0);
        }

        ctrl_c = 0;
        for (;;) {
            if (ctrl_c) {
                // TODO(20251220-002405): Is there perhaps a better way to cancel evaluation by utilizing long jumps from signal handlers?
                // Is that even legal?
                // https://www.gnu.org/savannah-checkouts/gnu/libc/manual/html_node/Longjmp-in-Handler.html
                printf("Evaluation canceled by user.\n");
                goto again;
            }

            gc(expr, bindings);

            Expr_Index expr1;
            if (!eval1(expr, &expr1)) goto again;
            if (expr.unwrap == expr1.unwrap) break;
            expr = expr1;
        }

        printf("RESULT: ");
        trace_expr(expr);
        printf("\n");
    }
quit:

    return 0;
}
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
