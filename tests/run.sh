#!/bin/sh
# shkbasic test suite.
#
#   sh tests/run.sh            run every test
#   sh tests/run.sh 04         run the tests whose name contains "04"
#   BLESS=1 sh tests/run.sh    record current output as expected
#
# Each tests/NN_name.bas is run with the framebuffer disabled and its
# combined stdout+stderr compared against tests/NN_name.expected.

set -e
cd "$(dirname "$0")/.."
BIN=./shkbasic
[ -x "$BIN" ] || { echo "build shkbasic first (make)"; exit 1; }

mkdir -p tests/out
filter="${1:-}"
pass=0
fail=0
failed=""

for src in tests/*.bas; do
  name=$(basename "$src" .bas)
  case "$name" in
    *"$filter"*) ;;
    *) continue ;;
  esac
  exp="tests/$name.expected"
  got="tests/out/$name.got"

  rm -f tests/out/scratch.txt tests/out/csv.txt
  SHKBASIC_NOFB=1 $BIN -n -s 1 "$src" >"$got" 2>&1 || true

  if [ -n "${BLESS:-}" ]; then
    cp "$got" "$exp"
    echo "bless $name"
    continue
  fi

  if [ ! -f "$exp" ]; then
    echo "MISSING expected output for $name (run with BLESS=1)"
    fail=$((fail + 1))
    failed="$failed $name"
  elif diff -u "$exp" "$got" >"tests/out/$name.diff" 2>&1; then
    echo "ok   $name"
    pass=$((pass + 1))
    rm -f "tests/out/$name.diff"
  else
    echo "FAIL $name"
    sed -n '1,40p' "tests/out/$name.diff"
    fail=$((fail + 1))
    failed="$failed $name"
  fi
done

[ -n "${BLESS:-}" ] && exit 0

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || { echo "failed:$failed"; exit 1; }
exit 0
