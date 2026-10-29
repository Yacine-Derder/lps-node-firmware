# Explicit stationary experiment. Invoke from repository root with -f this file.
# Both ALRs must use this image; receiver must accept propagation age6000.
EXTRA_CFLAGS += -DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RELATIVE_SPEED_MM_S=0 -DMUL_STATIONARY_CH5=1
include Makefile
