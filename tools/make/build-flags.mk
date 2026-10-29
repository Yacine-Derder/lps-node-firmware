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
