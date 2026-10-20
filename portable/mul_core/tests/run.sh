#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -f UPSTREAM.json ]; then python3 verify.py; fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -I. tests/test_core.c mul_core.c mul_common.c -o "$work/core"
"$work/core"
