#!/usr/bin/env bash
# Unit test for the vendored dvpackager's bc stand-in (used when bc is
# missing, e.g. MSYS2 / Git Bash): every expression form dvpackager pipes to
# bc, checked against what bc prints (numeric comparison).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$ROOT/Release/vendor/dvpackager/dvpackager"
# load only the stand-in block, forcing it even where a real bc exists
eval "$(sed -n '/^# --- vrecord Windows patch/,/^# -------------------/p' "$SCRIPT" | sed 's/^if ! command -v bc .*then$/if true ; then/')"
[[ "$(type -t bc)" == "function" ]] || { echo "FAIL: stand-in not loaded" ; exit 1 ; }
FAILS=0
t() {   # t <expected> <bc args> <expression>
    local expect="$1" args="$2" expr="$3" got
    # shellcheck disable=SC2086
    got="$(echo "$expr" | bc $args)"
    if awk -v a="$got" -v b="$expect" 'BEGIN { d = a - b ; if (d < 0) d = -d ; exit !(d < 1e-9) }' ; then
        echo "PASS  [$expr] = $got"
    else
        echo "FAIL  [$expr] = '$got' (expected $expect)" ; FAILS=$((FAILS + 1))
    fi
}
t 4                 "-l" "5-1"
t 151               ""   "250 - 100 + 1"
t 4                 "-l" "100/(25)"
t 1                 "-l" "if ((3003 - 1001)/2 == 1001) 1 else 0"
t 0                 "-l" "if ((3003 - 1000)/2 == 1001) 1 else 0"
t 1.4666333333333333 "-l" "(1.5 - (1/(30000/1001)) )"
t 1                 "-l" "0.040 >= -033"
t 1                 "-l" "-0.5 <= 033"
t 0                 "-l" "40 <= 033"
t 10.057            "-l" "10.05 + 0.007"
t 0.04              "-l" "1/(25)"
t 1.6666666666666667 ""  "scale=3;(7 - 2) / 3"
echo "=== ${FAILS} failure(s)"
exit $(( FAILS > 0 ? 1 : 0 ))
