#!/bin/sh
# CLI integration tests. Run after make.

UC=${UC:-./uc}

run=0
failed=0

report() {
    failed=$((failed + 1))
    printf 'FAIL %s\n' "$1" >&2
    shift
    for line in "$@"; do
        printf '  %s\n' "$line" >&2
    done
}

check() {
    name=$1
    want_status=$2
    want_out=$3
    shift 3

    run=$((run + 1))
    out=$("$@" 2>&1)
    got_status=$?

    if [ "$got_status" != "$want_status" ]; then
        report "$name" "status $got_status, want $want_status" "out: $out"
        return
    fi

    if [ "$out" != "$want_out" ]; then
        report "$name" "out: $out" "want: $want_out"
    fi
}

check_stdin() {
    name=$1
    want_status=$2
    want_out=$3
    input=$4
    shift 4

    run=$((run + 1))
    out=$(printf '%s' "$input" | "$@" 2>&1)
    got_status=$?

    if [ "$got_status" != "$want_status" ]; then
        report "$name" "status $got_status, want $want_status" "out: $out"
        return
    fi

    if [ "$out" != "$want_out" ]; then
        report "$name" "out: $out" "want: $want_out"
    fi
}

check_status() {
    name=$1
    want_status=$2
    shift 2

    run=$((run + 1))
    "$@" >/dev/null 2>&1
    got_status=$?

    if [ "$got_status" != "$want_status" ]; then
        report "$name" "status $got_status, want $want_status"
    fi
}

# Text arguments.
check "text argument" 0 "text" "$UC" "text"

check "two text arguments" 0 "one
two" "$UC" "one" "two"

check "text argument with dash dash" 0 "text" "$UC" -- "text"

check "dash dash stops option parsing" 0 "-s" "$UC" -- "-s"

# Standard input.
check_stdin "stdin" 0 "text" "text" "$UC"

check_stdin "stdin with dash" 0 "text" "text" "$UC" -

check_stdin "empty stdin" 0 "" "" "$UC"

check_stdin "stdin after text argument" 0 "text
more" "more" "$UC" "text" -

# Modes.
check "show mode" 0 "foo[U+00A0]bar" "$UC" -s "$(printf 'foo\302\240bar')"

check "clear mode" 0 "foo bar" "$UC" -c "$(printf 'foo\302\240bar')"

check "highlight mode is the default" 0 "foo$(printf '\357\277\275')bar" "$UC" "$(printf 'foo\302\240bar')"

# Exit codes.
check_status "check on clean input" 0 "$UC" --check "text"

check_status "check on problem input" 1 "$UC" --check "$(printf 'foo\302\240bar')"

check_status "check on clean stdin" 0 sh -c "printf 'text' | '$UC' --check"

check_status "unknown option" 2 "$UC" --bogus "text"

check_status "unknown short option" 2 "$UC" -x "text"

check_status "unknown encoding" 2 "$UC" -e koi8-r "text"

check_status "missing encoding argument" 2 "$UC" -e

check_status "mutually exclusive modes" 2 "$UC" -s -c "text"

check_status "help" 0 "$UC" --help

check_status "version" 0 "$UC" --version

# Color.
check "color always colorizes" 0 "foo$(printf '\033[0;31m')$(printf '\357\277\275')$(printf '\033[0m')bar" \
  "$UC" --color=always "$(printf 'foo\302\240bar')"

check "color never does not colorize" 0 "foo$(printf '\357\277\275')bar" \
  "$UC" --color=never "$(printf 'foo\302\240bar')"

check "color auto does not colorize a pipe" 0 "foo$(printf '\357\277\275')bar" \
  "$UC" --color=auto "$(printf 'foo\302\240bar')"

check "color with a separate argument" 0 "foo$(printf '\357\277\275')bar" \
  "$UC" --color never "$(printf 'foo\302\240bar')"

check_status "unknown color" 2 "$UC" --color=bogus "text"

check_status "missing color argument" 2 "$UC" --color

# Diacritic folding.
check "fold diacritics cafe" 0 "cafe" \
  "$UC" -c -e cp1252 --fold-diacritics "$(printf 'caf\303\251')"

check "fold diacritics keeps accent without the flag" 0 "$(printf 'caf\303\251')" \
  "$UC" -c -e cp1252 "$(printf 'caf\303\251')"

check "fold diacritics shows the problem" 0 "[U+00E9]" \
  "$UC" -s -e cp1252 --fold-diacritics "$(printf '\303\251')"

check "fold diacritics ascii encoding" 0 "cafe" \
  "$UC" -c -e ascii --fold-diacritics "$(printf 'caf\303\251')"

check_status "fold diacritics check finds problems" 1 \
  "$UC" --check -e cp1252 --fold-diacritics "$(printf 'caf\303\251')"

check_status "fold diacritics check passes without the flag" 0 \
  "$UC" --check -e cp1252 "$(printf 'caf\303\251')"

# Mixed script words.
check "clear repairs a confusable in a Latin word" 0 "hello" \
  "$UC" -c "$(printf 'hell\320\276')"

check "show prints a confusable in a Latin word" 0 "hell[U+043E]" \
  "$UC" -s "$(printf 'hell\320\276')"

check "clear keeps a pure non-Latin word" 0 "$(printf '\321\200\321\203\321\201')" \
  "$UC" -c -e cp1251 "$(printf '\321\200\321\203\321\201')"

check "clear repairs a confusable in a cp1251 Latin word" 0 "hello" \
  "$UC" -c -e cp1251 "$(printf 'hell\320\276')"

check_status "check finds a confusable in a Latin word" 1 \
  "$UC" --check "$(printf 'hell\320\276')"

# No input on a terminal prints the usage.
if command -v script >/dev/null 2>&1; then
    case $(uname -s) in
    Darwin)
        out=$(script -q /dev/null "$UC" 2>&1 | tr -d '\r')
        ;;
    *)
        out=$(script -qec "$UC" /dev/null 2>&1 | tr -d '\r')
        ;;
    esac

    run=$((run + 1))
    case "$out" in
    *"usage: uc"*) ;;
    *) report "no input on a terminal" "out: $out" ;;
    esac
fi

printf '%d tests run, %d failed\n' "$run" "$failed"
[ "$failed" -eq 0 ]
