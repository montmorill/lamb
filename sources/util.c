#include "lamb.h"

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

static struct {
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

bool symbol_eq(Symbol a, Symbol b)
{
    // NOTE: We compare addresses of the labels because they are expected to be interned with intern_label()
    return a.label == b.label && a.tag == b.tag;
}

Symbol symbol(const char *label)
{
    Symbol s = { .label = intern_label(label), .tag = 0 };
    return s;
}

Symbol symbol_fresh(Symbol s)
{
    static size_t global_counter = 0;
    s.tag = ++global_counter;
    return s;
}
