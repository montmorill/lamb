# Lamb for VS Code

Syntax highlighting, completion, language configuration and Lean-style Unicode
input for [Lamb](https://github.com/tsoding/lamb).

A TextMate grammar plus a small completion and Unicode input helper. No language
server, no build step.

## What it provides

- Syntax highlighting for `.lamb` files
- Completion for binding names, `:` commands, and `#` magics
- Binding parameters (`plus n m = ...`) highlighted like lambda parameters
- De Bruijn indices (`0`, `1`, ...) highlighted as numeric constants
- Anonymous binders (`\.`) highlighted like other lambda binders
- Bracket matching and auto-closing for `(` `)`
- `//` line comments (Ctrl+/ toggles)
- Lean-style Unicode input: `\forall` + **Tab** → `∀`
- `.lamb` file association (status bar shows **Lamb**)
- File icon for icon themes that do not already know Lamb (VS Code 1.64+)

## Unicode input

Type a backslash followed by an abbreviation and press **Tab**:

| Typed | Result |
| --- | --- |
| `\forall` | `∀` |
| `\exists` | `∃` |
| `\to` | `→` |
| `\lam` | `λ` |
| `\and` | `∧` |
| `\le` | `≤` |
| `\alpha` | `α` |

An underline appears under an abbreviation while **Tab** would convert it.
Typing `\fo` and pressing Tab also works: the shortest abbreviation starting
with the typed prefix (at least two characters) wins, as in Lean. One-character
prefixes are deliberately skipped so that lambda parameters such as `\f.` and
`\x.` can never be rewritten by accident.

Unlike Lean, abbreviations are **not** replaced eagerly while typing. `\` is the
Lamb lambda operator, so `\x.` and friends must stay exactly as typed;
replacement only happens when you explicitly press Tab.

- `Lamb: Insert Unicode Symbol` (`Ctrl+Alt+\`, `Cmd+Alt+\` on macOS) opens a
  searchable picker.
- `lamb.input.customTranslations` adds your own abbreviations, e.g.
  `{"foo": "☺"}` makes `\foo` insert `☺`.
- `lamb.input.leader` changes the leader character.
- `lamb.input.enabled` turns the mechanism off.

The full built-in table lives in `abbreviations.json`. Names may contain `_`,
`?`, `-`, and `'`, as well as Unicode letters and digits such as `α` or
`变量_1`; a name cannot start with a digit, since digit-initial tokens are De
Bruijn indices. `λ` is not a name character: it is the lambda operator,
equivalent to `\`. Other Unicode symbols such as `∀` or `→` are only valid
inside comments.
The interpreter classifies non-ASCII name characters with the C library's
`iswalnum` after selecting a UTF-8 locale when one is available, so the exact
set can differ slightly from the grammar's Unicode classes.

## Completion

Completion suggests:

- binding names defined in the current file and in the nearest `std.lamb`
  (next to the file, or in a workspace folder);
- REPL commands after `:`;
- magics after `#` (`#trace`, `#void`).

Type at least one character, then press **Tab** to accept the best match, or
**Ctrl+Space** to pick from the list. The extension enables
`editor.tabCompletion` for Lamb files by default, which you can override in your
settings. An empty prefix returns no items, so Tab still indents normally. When
a Unicode abbreviation is underlined, Tab converts the abbreviation instead.

Completion is disabled inside `//` comments. Binding names are read from the
current document and from `std.lamb`; other files in the workspace are not
scanned. Binding definitions use `name = body`.

## Develop

Open this directory (`editors/vscode`) in VS Code and press **F5** to launch an
Extension Development Host, then open `std.lamb` from the repository root in the
new window. After editing the grammar, press **Ctrl+R** in the dev host to reload.
After editing `extension.js`, restart the debugging session.

To inspect how a token is scoped, run **Developer: Inspect Editor Tokens and Scopes**
from the command palette and click the token.

### Installing without the dev host

```console
$ npx @vscode/vsce package
$ code --install-extension lamb-language-0.4.0.vsix
```

## Notes on the grammar

The one non-obvious rule is parameter detection. In `\x.y.x` **both** `x` and `y`
are parameters (the dropped-backslash shorthand), and the interpreter decides this
in `parse_fun_scope()` (`lamb.c`): after reading a name followed by `.` it peeks
two tokens and recurses if they are another name followed by `.`. The grammar
mirrors that same predicate, so "a name immediately followed by `.` is a
parameter" holds for valid code.

A binder may also be anonymous: `\.body`. The grammar starts a lambda region for
`\.` as well, so the dot is highlighted as a lambda separator even though there
is no parameter.

Binding definitions can also declare parameters. `plus n m = \f.x.m f (n f x);`
is sugar for `plus = \n.m.\f.x.m f (n f x);` (see `parse_binding_body()` in
`lamb.c`). The grammar therefore treats every name between the binding name and
`=` as a `variable.parameter.lamb`, exactly the scope used for lambda
parameters.

A run of digits such as `69` is a **De Bruijn index**, not a variable name.
Lamb has no numeric literals: `69` denotes a variable by index, not the number
69. The grammar scopes it as `constant.numeric.debruijn.lamb`. Since a name may
not start with a digit, the name rules use a `(?![\\p{Nd}])` lookahead;
sequences such as `hello69` are still single names, while invalid tokens such as
`1abc` are deliberately not highlighted as either kind of token.

Names are matched with `[\p{L}\p{N}_?'\-&&[^λ]]+` (Unicode letters and numbers
plus `_`, `?`, `-`, and `'`, excluding the reserved `λ`). The grammar accepts
the full Unicode set; the interpreter's non-ASCII set comes from the C
library's `iswalnum` and can be slightly narrower.

### Known limitations

- A lambda or binding header split across lines (the `.` or `=` on the following
  line) may not stay in one region, because VS Code tokenizes line by line.
- REPL transcript text such as `:save main.lamb` colors the filename as variables.
