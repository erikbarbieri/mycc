# --- toolchain ---
CXX      := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -g -O0
CPPFLAGS := -Iinclude
DEPFLAGS := -MMD -MP

# --- layout ---
SRC_DIR   := src
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/mycc.exe

# Find all .cpp files under src/
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Default target
all: $(TARGET)
build: $(TARGET)

# Link
$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Compile .cpp -> .o (and generate .d files)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

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

.PHONY: all build run clean
