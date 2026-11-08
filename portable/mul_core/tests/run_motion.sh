#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for policy in legacy bounded unknown stationary; do
 case "$policy" in
 legacy) flags='';;
 bounded) flags='-DMUL_PROP_MOTION_BUDGET_MM=100 -DMUL_RELATIVE_SPEED_MM_S=1000';;
 unknown) flags='-DMUL_PROP_MOTION_BUDGET_MM=100';;
 stationary) flags='-DMUL_PROP_MOTION_BUDGET_MM=0 -DMUL_RELATIVE_SPEED_MM_S=0';;
 esac
 cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
  -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RX_MOTION_GUARD=1 $flags -I. \
  tests/test_motion.c mul_clock_fit.c mul_clock.c mul_relation.c mul_common.c -lm -o "$work/motion"
 "$work/motion"
done
# Exercise the real cycle reconstruction and partial/missing/reset/reorder paths
# with the motion guard enabled, across all supported roster sizes/spacings.
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RX_MOTION_GUARD=1 -I. \
 tests/test_multi_receiver.c mul_clock_fit.c mul_multi_receiver.c mul_plan_protocol.c \
 mul_ranging_plan.c mul_multi_protocol.c mul_schedule.c mul_relation.c mul_legacy_twr.c \
 mul_clock.c mul_common.c mul_observation.c mul_downlink.c mul_localization.c -lm -o "$work/receiver"
"$work/receiver"
