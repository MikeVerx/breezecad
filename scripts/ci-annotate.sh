#!/usr/bin/env bash
# Turns compiler errors and failed test checks from CI logs into GitHub
# annotations, so they show on the run page and through the API (the logs
# themselves cannot always be downloaded).
# Usage: ci-annotate.sh build.log [test.log]
emit() { while IFS= read -r line; do echo "::error title=$1::${line//%/%25}"; done; }
for log in "$@"; do
    [ -f "$log" ] || continue
    name=$(basename "$log")
    grep -E "error|ERROR|FAILED|Failed|failed|FATAL|Assertion|terminate called|Segmentation|Exception|SEGFAULT|TEST CASE" "$log" \
        | grep -v "warning:" | head -40 | emit "$name"
    # A crash prints no check at all: the end of the test log says what ran last.
    if [ "$name" = "test.log" ] && grep -q "tests failed" "$log"; then tail -25 "$log" | emit "$name (tail)"; fi
done
