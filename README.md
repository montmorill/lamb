<div align="center">
    <img src="assets/LAMB_NBG_LOGO.png", width="200"/>
    <h1>Lamb</h1>
</div>

Tiny Pure Functional Programming Language in C. Based on [Untype Lambda Calculus](https://en.wikipedia.org/wiki/Lambda_calculus) with Normal Order reduction.

## Quick Start

```console
$ cc -o lamb lamb.c
$ ./lamb ./std.lamb
...
,---@>
 W-W'
Enter :help for more info
@> pair/make foo (pair/make bar baz)
RESULT: \b.b foo (\b.b bar baz)
@> xs = pair/make foo (pair/make bar baz)
Created binding xs
@> pair/fst xs
RESULT: foo
@> pair/snd xs
RESULT: \b.b bar baz
@> pair/fst (pair/snd xs)
RESULT: bar
@>
```

The REPL has built-in line editing, history, and Tab completion. Tab completes
binding names, `:` commands, `#` magics, and command arguments according to
their signatures, for example `:set debruijn ` suggests `on`, `off`, and
`full`. Input that is not a terminal (a pipe or a file) is read without editing
or completion.

On systems where the built-in editor is unavailable (for example a Windows
console emulator that does not expose a console handle), [rlwrap](https://github.com/hanslub42/rlwrap)
still provides Bash-style history and line navigation.

## Syntax

The syntax is based on the [Notation of Untype Lambda Calculus](https://en.wikipedia.org/wiki/Lambda_calculus_definition#Notation) and provides few improvements over it.

### Variables

Variable names may contain letters, digits, `_`, `?`, `-`, `'`, and `/`:

```
@> :ast x
[VAR] x
@> :ast hello69
[VAR] hello69
@> :ast is-empty?
[VAR] is-empty?
@> :ast x'
[VAR] x'
@> :ast list/filter
[VAR] list/filter
@>
```

A name cannot start with a digit. A token that starts with a digit is a [De Bruijn index](#de-bruijn-indices) instead, and mixing digits with name characters is an error:

```
@> :ast 69
[VAR] 69
@> :ast 1abc
1:6: ERROR: A name cannot start with a digit
@>
```

Unicode letters and digits are also accepted as name characters, as long as the first character is not a digit. The interpreter tries UTF-8 locales first and falls back to the system locale, delegating the classification of non-ASCII characters to the C library's `iswalnum`, so exactly which characters qualify depends on the platform. Error locations count characters rather than bytes.

`λ` is reserved as the lambda operator and cannot be used in a name.

`/` is a namespace separator by convention, as in `list/filter` or
`option/map`. It is an ordinary name character: the language does not scope
names by it, so `list/filter` is just a name that happens to contain a slash. A
name cannot start with `/`. Since `//` starts a comment, a name run stops before
it, so `foo// note` is the name `foo` followed by a comment.

`open`, `in`, and `section` are reserved keywords. They cannot be used as
variable names, lambda parameters, or binding names. Only the exact tokens are
reserved: qualified names that merely contain them, such as `list/open`, are
still ordinary names.

### Functions

To denote functions we use backslash `\`. The Greek small lambda `λ` is accepted as an equivalent alternative:

```
@> :ast \x.x
[FUN] \x
+--[VAR] x
@> :ast λx.x
[FUN] \x
+--[VAR] x
@> :ast \x.\y.x
[FUN] \x
+--[FUN] \y
   +--[VAR] x
@>
```

The body of the function extends as far right as possible. Use parenthesis to denote the boundaries of the function:

```
@> :ast \x.x x
[FUN] \x
+--[APP]
   +--[VAR] x
   +--[VAR] x
@> :ast (\x.x) x
[APP]
+--[FUN] \x
|  +--[VAR] x
+--[VAR] x
@>
```

Since the variable names can be longer than 1 character we can't use that silly mathematician trick of stitching their names together by saying that `\xy.x` means `\x.\y.x`, because in Lamb it just means it's a single function with a parameter `xy`:

```
@> :ast \xy.x
[FUN] \xy
+--[VAR] x
@>
```

Instead we allow you to drop consequent backslashes turning the dot `.` into a parameter separator:

```
@> :ast \x.y.x
[FUN] \x
+--[FUN] \y
   +--[VAR] x
@>
```

The parameter may be omitted entirely with `\.`, which creates an anonymous binder. Its variable can then only be referred to by index:

```
@> :ast \.0
[FUN] \.
+--[VAR] 0
@> \x.\.1
RESULT: \x.\.x
@>
```

Anonymous binders chain the same way: the rest of the backslashes can be dropped, so `\..1` means `\.\.1` and `\x..1` means `\x.\.1`. Since `.` cannot begin an expression, this is unambiguous:

```
@> :ast \..1
[FUN] \.
+--[FUN] \.
   +--[VAR] 1
@>
```

More generally, after the dot that ends a binder declaration, another `.` starts an anonymous binder and `name.` starts a named one.

### De Bruijn Indices

Internally, bound variables are De Bruijn indices; names only exist for our convenience. A numeric token is an index: number the surrounding binders from the inside out starting at `0`, and every index smaller than the number of binders refers to the corresponding binder:

```
@> \x.\y.x
RESULT: \x.\y.x
@> \x.\y.1
RESULT: \x.\y.x
@> \x.\y.y
RESULT: \x.\y.y
@> \x.\y.0
RESULT: \x.\y.y
@>
```

When printing, a variable is shown by the name of its binder whenever possible. If the binder is anonymous or its name is shadowed by a closer binder with the same name, the index is printed instead:

```
@> \x.\x.1
RESULT: \x.\x.1
@> \x.\.0
RESULT: \x.\.0
@> :ast \x.\.0
[FUN] \x
+--[FUN] \.
   +--[VAR] 0
@>
```

Substitution avoids capture by renaming binders when necessary. Here the free `y` would be captured by the binder `\y`, so the binder is renamed:

```
@> (\x.\y.x) y
RESULT: \y_1.y
@>
```

A number that is at least as large as the number of surrounding binders denotes a free variable. When such an index is printed, it is shifted by the number of binders, so that a term means the same thing after being saved and loaded again:

```
@> :ast 0
[VAR] 0
@> :ast \x.1
[FUN] \x
+--[VAR] 1
@>
```

The free variable written `0` at the top level is written `1` under one binder. The same shift applies to numeric atoms introduced through bindings, which are substituted textually:

```
@> a = 42
Created binding a
@> \x.a
RESULT: \x.43
@>
```

Use names rather than numbers for constants and free values to avoid this shift.

To control how variables are printed, use the `:set debruijn` command. Without a mode it toggles between `off` and `on`; `:set debruijn on`, `:set debruijn off`, and `:set debruijn full` set a mode explicitly:

```
@> :set debruijn
De Bruijn indices output: ON
@> \x.\y.x
RESULT: \x.\y.1
@> :set debruijn off
De Bruijn indices output: OFF
@> \x.\y.x
RESULT: \x.\y.x
@>
```

In `full` mode every binder is printed as `\.` and every variable occurrence as an index, so a closed term contains no names at all:

```
@> :set debruijn full
De Bruijn indices output: FULL
@> \x.\y.x
RESULT: \.\.1
@> :ast \x.\y.x
[FUN] \.
+--[FUN] \.
   +--[VAR] 1
@>
```

Free variables have no binder that could give them an index. They are numbered by their first appearance and printed like free indices (with the current binder depth added), so the output stays nameless even for open terms. For example, `foo bar` prints as `0 1`.

All three modes affect `:ast`, `:save`, and `:list` output.

To print binders with the Unicode lambda `λ` instead of `\`, use `:set unicode on`. Without an argument the command toggles the setting; `:set unicode off` restores `\`:

```
@> :set unicode on
Unicode lambda output: ON
@> \x.\y.x
RESULT: λx.λy.x
@> :ast \x.x
[FUN] λx
+--[VAR] x
@> :set unicode off
Unicode lambda output: OFF
@>
```

The setting affects every place a term is printed (`RESULT`, `:ast`, `:save`, and `:list`). Since `λ` is valid input, a file saved in Unicode mode loads back unchanged. The default is `off`.

To elide the symbol of chained binders, use `:set shorthand on`. The output uses the same dropped-backslash shorthand the parser accepts, for named and anonymous binders alike, so it round-trips:

```
@> :set shorthand on
Lambda shorthand output: ON
@> \x.\y.x
RESULT: \x.y.x
@> \.\.1
RESULT: \..1
@> :set unicode on
Unicode lambda output: ON
@> \x.\y.x
RESULT: λx.y.x
@> :set debruijn full
De Bruijn indices output: FULL
@> \x.\y.x
RESULT: λ..1
@>
```

`:debug`, `:save`, and `:list` use the same term printer and are affected as well; `:ast` is a tree dump with one binder per line, so it is not. The default is `off`.

## Applications

Just separate two lambda expressions with a space:

```
@> :ast (\x.x) x
[APP]
+--[FUN] \x
|  +--[VAR] x
+--[VAR] x
@> (\x.x) x
RESULT: x
@>
```

You can drop parenthesis if the expression is unambiguous:

```
@> :ast f \x.x
[APP]
+--[VAR] f
+--[FUN] \x
   +--[VAR] x
@>
```

Applications are left-associative:

```
@> :ast f a b c d
[APP]
+--[APP]
|  +--[APP]
|  |  +--[APP]
|  |  |  +--[VAR] f
|  |  |  +--[VAR] a
|  |  +--[VAR] b
|  +--[VAR] c
+--[VAR] d
@>
```

### Magics

Magics are special names that start with `#` and cannot be used as parameters of functions (that is they are always free). They perform various useful side effects.

`#trace` - when applied to a lambda expression it becomes a potential redex. When reduced it forces the reduction of its argument, prints the reduced argument to the console, and then returns it. Useful for debugging.

```
@> (#trace f) (#trace a) (#trace b) (#trace c)
TRACE: f
TRACE: a
TRACE: b
TRACE: c
RESULT: ((f a) b) c
@>
```

`#void` - when applied to a lambda expression it becomes a potential redex. When reduced it forces the reduction of its argument and returns itself. Useful when you only care about the side effects of the evaluation but not the result which might be long and useless anyway:

```console
$ ./lamb ./std.lamb
@> xs = list/cons foo (list/cons bar (list/cons baz (list/cons quux list/nil)))
Created binding xs
@> #void (list/map #trace xs)
TRACE: foo
TRACE: bar
TRACE: baz
TRACE: quux
RESULT: #void
```

## Bindings

You can take any expression and assign it to a name:

```
@> id = \x.x
Created binding id
@>
```

Then you can use that name instead of the expression:

```
@> id 69
RESULT: 69
@>
```

You can reassign the existing bindings at any moment

```
@> id = \y.y
Updated binding id
@>
```

To list all available bindings use `:list` command:

```
@> a = 69
Created binding a
@> b = 420
Created binding b
@> c = 1337
Created binding c
@> :list
id = \y.y;
a = 69;
b = 420;
c = 1337;
@>
```

### Semantics of Bindings

The bindings are not evaluated until you use them in an expression.

When you enter an expression which contains the names of the bindings into the REPL to evaluate, first things that happens is the binding names are substituted with their corresponding values before starting the evaluation. The bindings are substituted in a reversed order, so be careful with the order in which you create the bindings. Reassigning already existing bindings does not change their order. You have to `:delete` the binding first and create it again to affect its order.

Since each binding is applied only once you CANNOT use them for recursion

```
@> loop = loop
Created binding loop
@> loop
RESULT: loop
@>
```

Use [Y combinator](https://en.wikipedia.org/wiki/Fixed-point_combinator#Y_combinator) to organize the recursion:

```
@> Y = \f.(\x.f (x x)) (\x.f (x x))
Created binding Y
@> :debug  Y g
DEBUG: (\f.(\x.f (x x)) (\x.f (x x))) g
DEBUG: (\x.g (x x)) (\x.g (x x))
DEBUG: g ((\x.g (x x)) (\x.g (x x)))
DEBUG: g (g ((\x.g (x x)) (\x.g (x x))))
DEBUG: g (g (g ((\x.g (x x)) (\x.g (x x)))))
DEBUG: g (g (g (g ((\x.g (x x)) (\x.g (x x))))))
DEBUG: g (g (g (g (g ((\x.g (x x)) (\x.g (x x)))))))
DEBUG: g (g (g (g (g (g ((\x.g (x x)) (\x.g (x x))))))))
DEBUG: g (g (g (g (g (g (g ((\x.g (x x)) (\x.g (x x)))))))))
DEBUG: g (g (g (g (g (g (g (g ((\x.g (x x)) (\x.g (x x))))))))))
...
```

### Opening Namespaces

`open` brings the members of a namespace into scope, so that an unqualified name
resolves to `namespace/name` when that binding exists:

```
@> :load std.lamb
...
@> open nat;
Opened namespace nat
@> nat/eqb (add two three) five foo bar
RESULT: foo
```

Opening affects only how names are read; it does not create aliases or change
the names of new bindings. A local lambda parameter still wins over an opened
name. `open`, `in`, and `section` are reserved keywords, so unlike plain names they cannot
be shadowed by a parameter or used as a binding name.

Several namespaces can be opened at once:

```
@> open list nat;
Opened namespace list
Opened namespace nat
@> eqb (length (filter is-odd (replicate five one))) five foo bar
RESULT: foo
```

If a name is defined in more than one open namespace (or both bare and in an
open namespace), the interpreter reports an ambiguity. Qualify the name to
disambiguate:

```
@> open list option;
Opened namespace list
Opened namespace option
@> :ast filter
1:6: ERROR: Name `filter` is ambiguous between `list/filter` and `option/filter`
@> :ast list/filter
[VAR] list/filter
```

`open ... in ...` limits the opening to a single expression:

```
@> eqb (add two three) five foo bar
RESULT: eqb (add two three) five foo bar
@> open nat in eqb (add two three) five foo bar
RESULT: foo
```

The body of `open ... in` extends as far right as possible, like a lambda; use
parentheses to limit it. An unqualified name that no opened namespace defines is
left as written, so `id` keeps working while `nat` is open.

In a binding file `open` lasts until the end of that file and is scoped to it:
loading a file does not change the namespaces the REPL has open. In the REPL it
lasts until the session ends. The semicolon after `open` is required in a
binding file and optional in the REPL.

Opening does not add aliases to Tab completion: completion still works on the
full binding names, so type `nat/` to complete `nat/add`.

### Sections

`section` declares the namespace for the definitions that follow. It prefixes
their names and lets them refer to each other without repeating the prefix:

```
@> :load std.lamb
...
@> section list;
Entered section list
@> twin x = cons x (cons x nil)
Created binding list/twin
@> :list list/twin
list/twin = \x.list/cons x (list/cons x list/nil);
@> section;
Closed section
```

Inside a section, an unqualified name resolves to a member of the section
first. If the section has no such member, the name falls back to a bare binding
and then to the opened namespaces, so a section member shadows a binding of the
same name defined elsewhere. A local lambda parameter still wins over both.

Sections do not nest: `section` replaces the current section instead of nesting
inside it, and `section` without a name returns to the empty section. The name
itself may be qualified, in which case it is used as written:

```
@> section foo/bar;
Entered section foo/bar
@> nil f z = z
Created binding foo/bar/nil
```

A definition whose name already contains `/` is treated as fully qualified and
is not prefixed again. Since a section only prefixes new binding names and
opens the section for reading, it can be used freely together with `open`. In a
binding file a section lasts until the next `section` statement or the end of
the file; the semicolon is required there and optional in the REPL. `section`
is a reserved keyword, so unlike a plain name it cannot be shadowed by a
parameter or used as a binding name.

### Managing the Bindings Files

You can save current bindings to a file with `:save` command:

```
@> id = \x.x
Created binding id
@> const = \x.y.x
Created binding const
@> true = const
Created binding true
@> false = \x.y.y
Created binding false
@> :save main.lamb
Saved all the bindings to main.lamb
@> :q
$ cat main.lamb
id = \x.x;
const = \x.y.x;
true = const;
false = \x.y.y;
$
```

You can load the bindings with the `:load` command:

```
@> :load main.lamb
Created binding id
Created binding const
Created binding true
Created binding false
@> :list
id = \x.x;
const = \x.y.x;
true = const;
false = \x.y.y;
@>
```

Loading the bindings clears out all the previously defined bindings.

IMPORTANT! The bindings in the bindings file are separate with a semicolon `;`.

Repeating `:load` or `:save` without an argument applies it the last saved or loaded file (a.k.a. the active file).

Use `:edit` command to open and edit the bindings file in an external editor. The default editor is `vi` but you can set it with `$EDITOR` and `$LAMB_EDITOR` environment variable.

We provided a bunch of useful bindings in [std.lamb][./std.lamb]. They are
grouped by Church-encoded type: `bool/`, `pair/`, `nat/`, `list/`, and
`option/`; only `id` and `const` are unprefixed.

Passing a file as a command line argument to the interpreter acts as if you instantly `:load`-ed it.

```console
$ ./lamb ./main.lamb
Created binding id
Created binding const
Created binding true
Created binding false
,---@>
 W-W'
Enter :help for more info
@> :list
id = \x.x;
const = \x.y.x;
true = const;
false = \x.y.y;
@>
```

The file also automatically becames the active file. So `:load`, `:save`, and `:edit` will operate on it by default.

The interpreter can only work with one active file at a time right now.
