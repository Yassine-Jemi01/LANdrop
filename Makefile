CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CPPFLAGS ?=
LDFLAGS ?=
BUILD := build
BIN := $(BUILD)/landrop-net

.PHONY: all clean test help
all: $(BIN)

$(BIN): src/network/network.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@

test: $(BIN)
	sh tests/smoke.sh

clean:
	rm -rf $(BUILD)

help:
	@echo "make          Build the TCP transport helper"
	@echo "make test     Run local TCP transfer smoke tests"
	@echo "./scripts/landrop receive PORT DIRECTORY"
	@echo "./scripts/landrop send HOST PORT FILE"
