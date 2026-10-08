#!/usr/bin/env bash
# Turns compiler errors and failed test checks from CI logs into GitHub
# annotations, so they show on the run page and through the API.
# Usage: ci-annotate.sh build.log [test.log]
for log in "$@"; do
    [ -f "$log" ] || continue
    grep -E "error|ERROR|FAILED|Failed|failed|Assertion|terminate called|Segmentation" "$log" \
        | grep -v "warning:" | head -40 \
        | while IFS= read -r line; do echo "::error title=$(basename "$log")::${line//%/%25}"; done
done
