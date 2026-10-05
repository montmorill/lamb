#include "lamb.h"

void create_binding(Bindings *bindings, Symbol name, Expr_Index body)
{
    for (size_t i = 0; i < bindings->count; ++i) {
        if (symbol_eq(bindings->items[i].name, name)) {
            bindings->items[i].body = body;
            if (name.tag == 0) {
                printf("Updated binding %s\n", name.label);
            } else {
                printf("Updated binding %s:%zu\n", name.label, name.tag);
            }
            return;
        }
    }
    Binding binding = {
        .name = name,
        .body = body,
    };
    da_append(bindings, binding);
    if (name.tag == 0) {
        printf("Created binding %s\n", name.label);
    } else {
        printf("Created binding %s:%zu\n", name.label, name.tag);
    }
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
        Symbol name = symbol(l.string.items);
        if (!lexer_expect(&l, TOKEN_EQUALS)) return false;
        Expr_Index body;
        if (!parse_expr(&l, &body)) return false;
        if (!lexer_expect(&l, TOKEN_SEMICOLON)) return false;
        create_binding(bindings, name, body);
        if (!lexer_peek(&l)) return false;
    }
    return true;
}
