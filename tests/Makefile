# Host-side tests for LSN50V2 dendrometer firmware.
# Uses native gcc; no ARM, no HAL, no cross-compilation.

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O0 -g -I../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/inc
OBJDIR  := build

TESTS := harness_smoke dendrometer

.PHONY: all test clean
all: test

test: $(addprefix $(OBJDIR)/test_,$(TESTS))
	@set -e; for t in $^; do \
		printf "running %s ... " "$$t"; \
		"./$$t" || { echo FAIL; exit 1; }; \
	done
	@echo "all host tests passed"

$(OBJDIR)/test_harness_smoke: test_harness_smoke.c | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/test_dendrometer: test_dendrometer.c mock_board.c \
		../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/src/dendrometer.c \
		| $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ test_dendrometer.c mock_board.c \
		../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/src/dendrometer.c

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR)
