# --- toolchain ---
CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -g -O0
CPPFLAGS := -Iinclude
DEPFLAGS := -MMD -MP

# --- layout ---
SRC_DIR   := src
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/mycc.exe

# Find all .c files under src/
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Compile .c -> .o (and generate .d files)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Convenience targets
run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

# Include auto-generated dependency files
-include $(DEPS)

.PHONY: all run clean
