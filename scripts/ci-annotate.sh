#!/usr/bin/env bash
# Turns compiler errors and failed test checks from CI logs into GitHub
# annotations, so they show on the run page and through the API (the logs
# themselves cannot always be downloaded). GitHub keeps only ten annotations
# per step, so each block of lines goes out as one multi-line annotation.
# Usage: ci-annotate.sh build.log [test.log]
emit() {
    local text
    text=$(sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"} {print}')
    [ -n "$text" ] && echo "::error title=$1::$text"
}
for log in "$@"; do
    [ -f "$log" ] || continue
    name=$(basename "$log")
    # Lines cut short: a failed compile command alone can be kilobytes, and
    # an annotation over 64 KB is dropped without a word. The errors first:
    # warnings' context lines would otherwise fill the 60 lines.
    grep -E "error:|Error:|undefined reference|cannot find|DSO missing|FAILED:" "$log" | cut -c1-300 | head -40 | emit "$name (errors)"
    grep -E "error|Error|ERROR|FAILED|Failed|failed|FATAL|Assertion|terminate called|Segmentation|Exception|SEGFAULT|TEST CASE|undefined reference|ld: |cannot find|DSO missing" "$log" \
        | grep -v "warning:" | cut -c1-300 | head -60 | emit "$name"
    # The end of the build log says how it stopped, whatever the words.
    if [ "$name" = "build.log" ] || [ "$name" = "apk.log" ] || [ "$name" = "smoke.log" ]; then tail -25 "$log" | cut -c1-300 | emit "$name (tail)"; fi
    # A crash prints no check at all: the end of the test log says what ran last.
    if [ "$name" = "test.log" ] && grep -q "tests failed" "$log"; then tail -60 "$log" | emit "$name (tail)"; fi
    # A backtrace, when the crash was run again under gdb.
    if grep -q "^gdb: " "$log"; then grep "^gdb: " "$log" | cut -c1-160 | head -40 | emit "$name (backtrace)"; fi
    if grep -q "^crashes or fails: " "$log"; then grep "^crashes or fails: " "$log" | head -60 | emit "$name (cases)"; fi
done
