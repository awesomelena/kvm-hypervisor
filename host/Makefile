CC     = gcc
CFLAGS = -Wall -Wextra

BUILD_DIR ?= build

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

BIN = $(BUILD_DIR)/hypervisor

.PHONY: all clean

all: $(BUILD_DIR) $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinc -MMD -MP -c -o $@ $<

$(BIN): $(OBJS)
	$(CC) $(OBJS) -lpthread -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
