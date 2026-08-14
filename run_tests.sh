#!/usr/bin/env bash
cd "$(dirname "$0")"

# Locate the built flex binary (flex.exe on Windows/MSYS, flex elsewhere).
if [ -x ./flex-2.6.4/src/flex.exe ]; then
    FLEX=./flex-2.6.4/src/flex.exe
else
    FLEX=./flex-2.6.4/src/flex
fi
if [ ! -x "$FLEX" ]; then
    echo "flex binary not found at $FLEX; run 'make' first" >&2
    exit 1
fi

run() {
    local name="$1" lfile="$2" input="$3"
    local out=/tmp/fx-$name.c bin=/tmp/fx-$name.exe
    "$FLEX" -o "$out" "$lfile" 2>/tmp/fx-$name.err
    local frc=$?
    gcc -o "$bin" "$out" 2>>/tmp/fx-$name.err
    local grc=$?
    echo "=== $name (flex=$frc gcc=$grc) ==="
    if [ $frc -eq 0 ] && [ $grc -eq 0 ]; then
        printf '%s' "$input" | "$bin" 2>&1
    else
        cat /tmp/fx-$name.err
    fi
    echo
}

# run_expect asserts that the produced output contains $4 (regression guard).
run_expect() {
    local name="$1" lfile="$2" input="$3" want="$4"
    local out=/tmp/fx-$name.c bin=/tmp/fx-$name.exe
    "$FLEX" -o "$out" "$lfile" 2>/tmp/fx-$name.err
    local frc=$?
    gcc -o "$bin" "$out" 2>>/tmp/fx-$name.err
    local grc=$?
    if [ $frc -ne 0 ] || [ $grc -ne 0 ]; then
        echo "=== $name (flex=$frc gcc=$grc) BUILD FAILED ==="
        cat /tmp/fx-$name.err
        return 1
    fi
    local got
    got=$(printf '%s' "$input" | "$bin" 2>&1)
    if printf '%s' "$got" | grep -qF "$want"; then
        echo "=== $name PASS (output contains: $want) ==="
        printf '%s\n' "$got"
        return 0
    fi
    echo "=== $name FAIL: expected output containing: $want ==="
    printf '%s\n' "$got"
    return 1
}

rc=0
run uni    tests/example-unicode.l '你好世界 αβγδ → ★ ☀ hello 中文
'
run single tests/example-single.l   '中 AabcX é中 𠀀 😀 aZ 文
'
run_expect range tests/example-range.l '你好 中 举 乀 hello
' 'CJK: 你好'
rc=$((rc || $?))
exit $rc
