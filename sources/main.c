#include "lamb.h"

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
    while (path_count > 0 && isspace(*path_data)) {
        path_data++;
        path_count--;
    }
    while (path_count > 0 && isspace(path_data[path_count - 1])) {
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
                    assert(bindings.items[i].name.tag == 0);
                    sb_appendf(&sb, "%s = ", bindings.items[i].name.label);
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
                        assert(bindings.items[i].name.tag == 0);
                        sb.count = 0;
                        sb_appendf(&sb, "%s = ", bindings.items[i].name.label);
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
                        assert(bindings.items[i].name.tag == 0);
                        if (bindings.items[i].name.label == label) {
                            sb.count = 0;
                            sb_appendf(&sb, "%s = ", bindings.items[i].name.label);
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
                Symbol name = symbol(l.string.items);
                for (size_t i = 0; i < bindings.count; ++i) {
                    if (symbol_eq(bindings.items[i].name, name)) {
                        da_delete_at(&bindings, i);
                        printf("Deleted binding %s\n", name.label);
                        goto again;
                    }
                }
                printf("ERROR: binding %s was not found\n", name.label);
                goto again;
            }
            if (command(&commands, l.string.items, "debug", "<expr>", "Step debug the evaluation of an expression")) {
                Expr_Index expr;
                if (!parse_expr(&l, &expr)) goto again;
                if (!lexer_expect(&l, TOKEN_END)) goto again;
                for (size_t i = bindings.count; i > 0; --i) {
                    expr = replace(bindings.items[i-1].name, expr, bindings.items[i-1].body);
                }

                ctrl_c = 0;
                for (;;) {
                    if (ctrl_c) goto again; // TODO(20251220-002405)

                    printf("DEBUG: ");
                    trace_expr(expr);
                    printf("\n");

                    printf("-> ");
                    fflush(stdin);

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

        Token_Kind a, b;
        Cur cur = l.cur; {
            if (!lexer_next(&l)) goto again;
            a = l.token;
            if (!lexer_next(&l)) goto again;
            b = l.token;
        } l.cur = cur;

        if (a == TOKEN_NAME && b == TOKEN_EQUALS) {
            if (!lexer_expect(&l, TOKEN_NAME)) goto again;
            Symbol name = symbol(l.string.items);
            if (!lexer_expect(&l, TOKEN_EQUALS)) goto again;
            Expr_Index body;
            if (!parse_expr(&l, &body)) goto again;
            if (!lexer_expect(&l, TOKEN_END)) goto again;
            create_binding(&bindings, name, body);
            goto again;
        }

        Expr_Index expr;
        if (!parse_expr(&l, &expr)) goto again;
        if (!lexer_expect(&l, TOKEN_END)) goto again;
        for (size_t i = bindings.count; i > 0; --i) {
            expr = replace(bindings.items[i-1].name, expr, bindings.items[i-1].body);
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
