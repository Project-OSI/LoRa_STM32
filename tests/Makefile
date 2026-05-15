# Host-side tests for LSN50V2 firmware.
# Uses native gcc; no ARM, no HAL, no cross-compilation.

CC      ?= gcc

# Directory containing DRAGINO-LRWAN(AT) has literal parens in its name.
# GNU Make can't match a file prerequisite whose name contains unescaped parens
# via the \( escape in prerequisite lists, so we build the path from parts.
LPAREN  := (
RPAREN  := )
PROJ_BASE := ../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN$(LPAREN)AT$(RPAREN)

CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O0 -g

OBJDIR  := build

TESTS := harness_smoke chameleon_payload chameleon_driver chameleon_dummy battery_level_typo chameleon_settle_and_retry

.PHONY: all test clean
all: test

test: $(addprefix $(OBJDIR)/test_,$(TESTS))
	@set -e; for t in $^; do \
		printf "running %s ... " "$$t"; \
		"./$$t" || { echo FAIL; exit 1; }; \
	done
	@echo "all host tests passed"

$(OBJDIR)/test_harness_smoke: test_harness_smoke.c | $(OBJDIR)
	$(CC) $(CFLAGS) "-I$(PROJ_BASE)/inc" -o $@ $<

$(OBJDIR)/test_chameleon_payload: test_chameleon_payload.c \
		$(PROJ_BASE)/src/chameleon_payload.c \
		| $(OBJDIR)
	$(CC) $(CFLAGS) "-I$(PROJ_BASE)/inc" -o $@ \
		test_chameleon_payload.c "$(PROJ_BASE)/src/chameleon_payload.c"

$(OBJDIR)/test_chameleon_driver: test_chameleon_driver.c mock_chameleon_i2c.c \
		$(PROJ_BASE)/src/via_chameleon.c \
		$(PROJ_BASE)/src/chameleon_payload.c \
		| $(OBJDIR)
	$(CC) $(CFLAGS) "-I$(PROJ_BASE)/inc" "-I." -o $@ \
		test_chameleon_driver.c mock_chameleon_i2c.c \
		"$(PROJ_BASE)/src/via_chameleon.c" \
		"$(PROJ_BASE)/src/chameleon_payload.c"

$(OBJDIR)/test_chameleon_settle_and_retry: test_chameleon_settle_and_retry.c mock_chameleon_i2c.c \
		$(PROJ_BASE)/src/via_chameleon.c \
		$(PROJ_BASE)/src/chameleon_payload.c \
		| $(OBJDIR)
	$(CC) $(CFLAGS) "-I$(PROJ_BASE)/inc" "-I." -o $@ \
		test_chameleon_settle_and_retry.c mock_chameleon_i2c.c \
		"$(PROJ_BASE)/src/via_chameleon.c" \
		"$(PROJ_BASE)/src/chameleon_payload.c"

$(OBJDIR)/test_chameleon_dummy: test_chameleon_dummy.c mock_chameleon_i2c.c \
		$(PROJ_BASE)/src/via_chameleon.c \
		$(PROJ_BASE)/src/chameleon_payload.c \
		| $(OBJDIR)
	$(CC) $(CFLAGS) -DCHAMELEON_DUMMY "-I$(PROJ_BASE)/inc" "-I." -o $@ \
		test_chameleon_dummy.c mock_chameleon_i2c.c \
		"$(PROJ_BASE)/src/via_chameleon.c" \
		"$(PROJ_BASE)/src/chameleon_payload.c"

$(OBJDIR)/test_battery_level_typo: test_battery_level_typo.c | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR)
