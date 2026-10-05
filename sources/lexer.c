#include "lamb.h"

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
    case TOKEN_MAGIC:     return "TOKEN_MAGIC";
    default: UNREACHABLE("Token_Kind");
    }
}

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

void lexer_trim_left(Lexer *l)
{
    while (isspace(lexer_curr_char(l))) {
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

bool issymbol(int x)
{
    return isalnum(x) || x == '_';
}

bool lexer_next(Lexer *l)
{
    for (;;) {
        lexer_trim_left(l);
        if (lexer_starts_with(l, "//")) lexer_drop_line(l);
        else break;
    }

    l->row = l->cur.row + 1;
    l->col = l->cur.pos - l->cur.bol + 1;

    char x = lexer_next_char(l);
    if (x == '\0') {
        l->token = TOKEN_END;
        return true;
    }

    switch (x) {
    case '(':  l->token = TOKEN_OPAREN;    return true;
    case ')':  l->token = TOKEN_CPAREN;    return true;
    case '\\': l->token = TOKEN_LAMBDA;    return true;
    case '.':  l->token = TOKEN_DOT;       return true;
    case ':':  l->token = TOKEN_COLON;     return true;
    case ';':  l->token = TOKEN_SEMICOLON; return true;
    case '=':  l->token = TOKEN_EQUALS;    return true;
    }

    if (x == '#') {
        l->token = TOKEN_MAGIC;
        l->string.count = 0;
        while (issymbol(lexer_curr_char(l))) {
            x = lexer_next_char(l);
            da_append(&l->string, x);
        }
        sb_append_null(&l->string);
        return true;
    }

    if (issymbol(x)) {
        l->token = TOKEN_NAME;
        l->string.count = 0;
        da_append(&l->string, x);
        while (issymbol(lexer_curr_char(l))) {
            x = lexer_next_char(l);
            da_append(&l->string, x);
        }
        sb_append_null(&l->string);
        return true;
    }

    l->token = TOKEN_INVALID;
    lexer_print_loc(l, stderr);
    fprintf(stderr, "ERROR: Unknown token starts with `%c`\n", x);
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
