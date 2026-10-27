# Lamb for VS Code

Syntax highlighting and language configuration for [Lamb](https://github.com/tsoding/lamb).

Grammar-only extension: TextMate grammar, no language server, no build step.

## What it provides

- Syntax highlighting for `.lamb` files
- Bracket matching and auto-closing for `(` `)`
- `//` line comments (Ctrl+/ toggles)
- `.lamb` file association (status bar shows **Lamb**)
- File icon for icon themes that do not already know Lamb (VS Code 1.64+)

## Develop

Open this directory (`editors/vscode`) in VS Code and press **F5** to launch an
Extension Development Host, then open `std.lamb` from the repository root in the
new window. After editing the grammar, press **Ctrl+R** in the dev host to reload.

To inspect how a token is scoped, run **Developer: Inspect Editor Tokens and Scopes**
from the command palette and click the token.

### Installing without the dev host

```console
$ npx @vscode/vsce package
$ code --install-extension lamb-language-0.1.0.vsix
```

## Notes on the grammar

The one non-obvious rule is parameter detection. In `\x.y.x` **both** `x` and `y`
are parameters (the dropped-backslash shorthand), and the interpreter decides this
in `parse_fun()` (`lamb.c`): after reading `NAME DOT` it peeks two tokens and
recurses if they are another `NAME DOT`. The grammar mirrors that same predicate,
so "a name immediately followed by `.` is a parameter" holds for valid code.

Also note that a run of digits such as `69420` is a **variable name**, not a
numeric literal — Lamb has no numeric tokens at all. The grammar deliberately
contains no `constant.numeric` rule.

### Known limitations

- A lambda header split across lines (the `.` on the following line) may not stay
  in one region, because VS Code tokenizes line by line.
- REPL transcript text such as `:save main.lamb` colors the filename as variables.
