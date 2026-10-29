# Make does not otherwise notice changed command-line flags. Preserve timestamps
# when identical; invalidate every object when effective compile/link flags change.
.PHONY: force_build_flags
bin:
	@mkdir -p $@
bin/.build-flags: force_build_flags | bin
	@mkdir -p bin
	@$(file >bin/.build-flags.tmp,$(CC) $(CFLAGS) $(LDFLAGS)) true
	@cmp -s $@ $@.tmp && rm $@.tmp || mv $@.tmp $@
$(OBJS): bin/.build-flags

# Show actual preprocessor options even for an up-to-date image. A plain make
# returning to strict policy must be visible before an operator programs it.
.PHONY: show_mul_build_options
all dfu: show_mul_build_options
show_mul_build_options:
	@$(CC) $(CFLAGS) -dM -E -x c -include portable/mul_core/mul_propagation.h /dev/null | awk '$$2=="MUL_PROP_MAX_AGE_MS" || $$2=="MUL_RELATIVE_SPEED_MM_S" || $$2=="MUL_HYBRID_CH5" || $$2=="MUL_STATIONARY_CH5" {printf "MUL effective: %s=%s\n",$$2,$$3}'
