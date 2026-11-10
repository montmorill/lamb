#!/usr/bin/env bash
# Regression tests for Unicode identifiers in the lexer.
#
# Build the interpreter first:
#   cc -o lamb lamb.c
# Then run:
#   tests/utf8-names.sh [path-to-lamb]

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${1:-$root/lamb}
if [ ! -x "$bin" ] && [ -x "$bin.exe" ]; then
    bin=$bin.exe
fi
if [ ! -x "$bin" ]; then
    echo "error: cannot find the lamb binary; build it with 'cc -o lamb lamb.c'" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

passed=0
failed=0

ok() { passed=$((passed + 1)); }

fail() { # fail <label> <output>
    failed=$((failed + 1))
    echo "FAIL: $1"
    printf '%s\n' "$2" | sed 's/^/    /'
}

repl() { # repl <input> [bindings-file]
    if [ $# -gt 1 ]; then
        printf '%s' "$1" | "$bin" "$2" 2>&1
    else
        printf '%s' "$1" | "$bin" 2>&1
    fi
}

ast() { repl ":ast $1
:quit
"; }

check_contains() { # check_contains <label> <output> <fragment>
    # LC_ALL=C keeps grep on a byte comparison; some MSYS grep builds fail to
    # match characters outside the Basic Multilingual Plane otherwise.
    if printf '%s' "$2" | LC_ALL=C grep -Fq -- "$3"; then ok; else fail "$1" "$2"; fi
}

check_name() { # check_name <label> <name>
    check_contains "$1" "$(ast "$2")" "[VAR] $2"
}

# --- accepted names -------------------------------------------------------

check_name "ASCII" "hello69"
check_name "Greek letter" "α"
check_name "Cyrillic word" "Привет"
check_name "Han characters" "变量"
check_name "precomposed accent" "café"
check_name "combining accent" $'cafe\xcc\x81'
check_name "combining tilde" $'nam\xcc\x83e'
check_name "Devanagari vowel sign" $'\xe0\xa4\x95\xe0\xa4\xbf'
check_name "BMP letter number" "Ⅻ"
check_name "fullwidth letter" "Ａ"
check_name "Unicode digit after first char" $'a\xd9\xa3'
check_name "Unicode digit at start" $'\xd9\xa3abc'
check_name "leading combining mark" $'\xcc\x81'
check_name "CJK punctuation" "。"
check_name "emoji" $'\xf0\x9f\x98\x80'
check_name "astral symbol" $'\xf0\x9d\x84\x9e'
check_name "astral letter" $'\xf0\x9d\x90\x80'
check_name "astral letter with ASCII" $'\xf0\x9d\x90\x80x'

check_contains "lambda binder" "$(ast '\变量.变量')" '[FUN] \变量'
check_contains "lambda binder body" "$(ast '\变量.变量')" '[VAR] 变量'
check_contains "unicode lambda keyword" "$(ast 'λx.x')" '[FUN] \x'
check_contains "namespace separator" "$(ast 'list/filter')" '[VAR] list/filter'
check_contains "comment after name" "$(ast 'foo//comment')" '[VAR] foo'

# --- rejected names -------------------------------------------------------

check_contains "ASCII digit at start" "$(ast '1abc')" "A name cannot start with a digit"
check_contains "ideographic space" "$(ast $'\xe3\x80\x80x')" "Unknown token"
check_contains "no-break space" "$(ast $'\xc2\xa0x')" "Unknown token"
check_contains "zero-width space" "$(ast $'\xe2\x80\x8bx')" "Unknown token"
check_contains "C1 control" "$(ast $'\xc2\x85x')" "Unknown token"

# --- bindings, saving, and loading ---------------------------------------

decomposed=$'cafe\xcc\x81'
astral=$'\xf0\x9d\x90\x80'
binding_out=$(
    cd "$tmp" || exit 1
    printf '%s = \\x.x\n%s = \\y.y\n:save names.lamb\n:list\n:quit\n' "$decomposed" "$astral" | "$bin" 2>&1
)
check_contains "bind combining name" "$binding_out" "Created binding $decomposed"
check_contains "bind astral name" "$binding_out" "Created binding $astral"
check_contains "save" "$binding_out" "Saved all the bindings"
check_contains "list combining name" "$binding_out" "$decomposed = \x.x;"
check_contains "list astral name" "$binding_out" "$astral = \y.y;"

saved=$(cat "$tmp/names.lamb")
check_contains "saved combining name" "$saved" "$decomposed = \x.x;"
check_contains "saved astral name" "$saved" "$astral = \y.y;"

reload_out=$(cd "$tmp" && printf ':list\n:quit\n' | "$bin" names.lamb 2>&1)
check_contains "reload combining name" "$reload_out" "$decomposed = \x.x;"
check_contains "reload astral name" "$reload_out" "$astral = \y.y;"

# --- summary --------------------------------------------------------------

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
