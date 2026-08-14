#!/usr/bin/env bash
cd "$(dirname "$0")"
FLEX=./flex-2.6.4/src/flex.exe
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
run uni    tests/example-unicode.l '你好世界 αβγδ → ★ ☀ hello 中文
'
run single tests/example-single.l   '中 AabcX é中 𠀀 😀 aZ 文
'
