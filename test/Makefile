CC       = cc
CFLAGS   = -std=c11 -Wall -Wextra -I.. -g
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
TSANFLAGS = -fsanitize=thread -fno-omit-frame-pointer

RC_TESTS   = test_rc_strong test_rc_weak
ARC_TESTS  = test_arc_strong test_arc_weak test_arc_mutex test_arc_threaded
ALL_TESTS  = $(RC_TESTS) $(ARC_TESTS) test_alignment

# TSan targets (arc only; rc is single-threaded, TSan adds no value there)
TSAN_BINS  = $(ARC_TESTS:%=%_tsan)

# m32 targets — recompile everything with -m32 to verify 32-bit behaviour
M32_BINS   = $(ALL_TESTS:%=%-m32)

.PHONY: all test tsan m32 clean

all: $(ALL_TESTS)

test: $(ALL_TESTS)
	@echo "Running tests (ASAN + UBSan)..."; echo ""
	@ok=0; fail=0; \
	for t in $(ALL_TESTS); do \
		printf "  %-32s" "$$t"; \
		if ./$$t > /dev/null 2>&1; then \
			echo "ok"; ok=$$((ok+1)); \
		else \
			echo "FAILED"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "$$ok passed, $$fail failed"; \
	[ $$fail -eq 0 ]

tsan: $(TSAN_BINS)
	@echo "Running tests (TSan)..."; echo ""
	@ok=0; fail=0; \
	for t in $(TSAN_BINS); do \
		printf "  %-37s" "$$t"; \
		if ./$$t > /dev/null 2>&1; then \
			echo "ok"; ok=$$((ok+1)); \
		else \
			echo "FAILED"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "$$ok passed, $$fail failed"; \
	[ $$fail -eq 0 ]

# rc tests: no pthread needed
$(RC_TESTS): %: %.c ../rc.h
	$(CC) $(CFLAGS) $(SANFLAGS) $< -o $@

# arc tests: require pthread
$(ARC_TESTS): %: %.c ../arc.h
	$(CC) $(CFLAGS) $(SANFLAGS) $< -o $@ -lpthread

# alignment test: includes both headers, needs pthread and libm (fabs)
test_alignment: test_alignment.c ../rc.h ../arc.h
	$(CC) $(CFLAGS) $(SANFLAGS) $< -o $@ -lpthread -lm

# TSan variants of arc tests
$(TSAN_BINS): %_tsan: %.c ../arc.h
	$(CC) $(CFLAGS) $(TSANFLAGS) $< -o $@ -lpthread

# 32-bit variants — requires lib32-glibc and multilib toolchain
m32: $(M32_BINS)
	@echo "Running tests (-m32)..."; echo ""
	@ok=0; fail=0; \
	for t in $(M32_BINS); do \
		printf "  %-37s" "$$t"; \
		if ./$$t > /dev/null 2>&1; then \
			echo "ok"; ok=$$((ok+1)); \
		else \
			echo "FAILED"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "$$ok passed, $$fail failed"; \
	[ $$fail -eq 0 ]

$(RC_TESTS:%=%-m32): %-m32: %.c ../rc.h
	$(CC) $(CFLAGS) -m32 $(SANFLAGS) $< -o $@

$(ARC_TESTS:%=%-m32): %-m32: %.c ../arc.h
	$(CC) $(CFLAGS) -m32 $(SANFLAGS) $< -o $@ -lpthread

test_alignment-m32: test_alignment.c ../rc.h ../arc.h
	$(CC) $(CFLAGS) -m32 $(SANFLAGS) $< -o $@ -lpthread -lm

clean:
	rm -f $(ALL_TESTS) $(TSAN_BINS) $(M32_BINS)
