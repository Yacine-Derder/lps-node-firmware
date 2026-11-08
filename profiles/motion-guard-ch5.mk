# Experimental conservative path-change guard. No EEPROM/plan/PHY migration.
# Speed must bound relative path-length change; UNKNOWN rejects correction.
MUL_MOTION_SPEED_MM_S ?= -1
MUL_MOTION_BUDGET_MM ?= 100
override EXTRA_CFLAGS += -DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RELATIVE_SPEED_MM_S=$(MUL_MOTION_SPEED_MM_S) -DMUL_PROP_MOTION_BUDGET_MM=$(MUL_MOTION_BUDGET_MM)
include Makefile
# Share bounded integer mapping helpers to fit the 128KB target. Kalman's
# compilation, doubles, workspace, tasks, stacks and queues remain unchanged.
src/mul_clock.o src/mul_relation.o: CFLAGS += -flto
