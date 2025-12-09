# Custom Makefile for Sparkle Duck project.

# Default target.
.DEFAULT_GOAL := help

# Build directories (separate for debug and release).
BUILD_DEBUG_DIR := build-debug
BUILD_RELEASE_DIR := build-release
BUILD_DIR := build-debug  # Default to debug for backward compatibility.
BIN_DIR := bin

# Cross-compilation directories (aarch64).
BUILD_CROSS_DEBUG_DIR := build-aarch64-debug
BUILD_CROSS_RELEASE_DIR := build-aarch64-release
CROSS_TOOLCHAIN := cmake/aarch64-toolchain.cmake

# Number of parallel jobs (use all available cores).
JOBS := $(shell nproc 2>/dev/null || echo 4)

# Check if ccache is available.
CCACHE := $(shell which ccache 2>/dev/null)

# Build timing - stores start time in a temp file.
BUILD_START_FILE := /tmp/.sparkle-duck-build-start

# Helper to start build timer and zero ccache stats.
define start_build
	@date +%s > $(BUILD_START_FILE)
	@if [ -n "$(CCACHE)" ]; then ccache --zero-stats > /dev/null 2>&1; fi
endef

# Helper to show build stats (time and ccache) at end of build.
define show_build_stats
	@START=$$(cat $(BUILD_START_FILE) 2>/dev/null || echo 0); \
	END=$$(date +%s); \
	ELAPSED=$$((END - START)); \
	MINS=$$((ELAPSED / 60)); \
	SECS=$$((ELAPSED % 60)); \
	if [ $$MINS -gt 0 ]; then \
		TIME_STR="$${MINS}m $${SECS}s"; \
	else \
		TIME_STR="$${SECS}s"; \
	fi; \
	if [ -n "$(CCACHE)" ]; then \
		STATS=$$(ccache --print-stats 2>/dev/null); \
		DIRECT=$$(echo "$$STATS" | grep "^direct_cache_hit" | cut -f2); \
		PREPROC=$$(echo "$$STATS" | grep "^preprocessed_cache_hit" | cut -f2); \
		MISSES=$$(echo "$$STATS" | grep "^cache_miss" | cut -f2); \
		DIRECT=$${DIRECT:-0}; PREPROC=$${PREPROC:-0}; MISSES=$${MISSES:-0}; \
		HITS=$$((DIRECT + PREPROC)); \
		TOTAL=$$((HITS + MISSES)); \
		if [ $$TOTAL -gt 0 ]; then \
			PCT=$$((HITS * 100 / TOTAL)); \
			echo "⏱️  $$TIME_STR  📦 ccache: $$HITS/$$TOTAL hits ($$PCT%)"; \
		else \
			echo "⏱️  $$TIME_STR"; \
		fi; \
	else \
		echo "⏱️  $$TIME_STR"; \
	fi
endef

# Test binary names.
TEST_BINARY := sparkle-duck-tests
TEST_SLOW_BINARY := sparkle-duck-tests-slow
MAIN_BINARY := sparkle-duck

.PHONY: all clean debug release asan build-tests build-tests-slow test test-slow test-all visual-tests run run-asan test-asan format format-check lint lint-fix check help cross-debug cross-release cross-sysroot

all: release

release:
	@echo "Building release version..."
	$(call start_build)
	@cmake -B $(BUILD_RELEASE_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	@$(MAKE) -C $(BUILD_RELEASE_DIR) -j$(JOBS)
	$(call show_build_stats)

release-verify: release
	@echo "Running tests to verify build..."
	@./$(BUILD_RELEASE_DIR)/$(BIN_DIR)/$(TEST_BINARY)

debug:
	@echo "Building debug version..."
	$(call start_build)
	@cmake -B $(BUILD_DEBUG_DIR) -S . -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-g -O0 -DLOG_DEBUG" \
		-DCMAKE_CXX_FLAGS="-g -O0 -DLOG_DEBUG"
	@$(MAKE) -C $(BUILD_DEBUG_DIR) -j$(JOBS)
	$(call show_build_stats)

asan:
	@echo "Building debug version with AddressSanitizer..."
	$(call start_build)
	@cmake -B $(BUILD_DEBUG_DIR) -S . -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-g -O0 -DLOG_DEBUG -fsanitize=address -fno-omit-frame-pointer" \
		-DCMAKE_CXX_FLAGS="-g -O0 -DLOG_DEBUG -fsanitize=address -fno-omit-frame-pointer" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address"
	@$(MAKE) -C $(BUILD_DEBUG_DIR) -j$(JOBS)
	$(call show_build_stats)
	@echo "ASAN build complete. Use 'make run-asan' or 'make test-asan' to run with AddressSanitizer."

build-tests: debug
	@echo "Test binary built successfully: $(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY)"

build-tests-slow: debug
	@echo "Slow test binary built successfully: $(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_SLOW_BINARY)"

test: build-tests
	@echo "Running tests..."
	@./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY) $(ARGS)

test-slow: build-tests-slow
	@echo "Running slow tests..."
	@./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_SLOW_BINARY) $(ARGS)

test-asan: asan
	@echo "Running tests with AddressSanitizer..."
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:print_stats=1 ./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY) $(ARGS)

test-all: test test-slow visual-tests

visual-tests:
	@echo "Running visual tests..."
	@if [ ! -f "./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY)" ]; then \
		echo "Error: Test binary not found. Run 'make debug' first."; \
		exit 1; \
	fi
	@if [ -z "$$DISPLAY" ] && [ -z "$$WAYLAND_DISPLAY" ]; then \
		echo "Warning: No display detected. Visual tests may not work properly."; \
	fi
	@SPARKLE_DUCK_VISUAL_TESTS=1 ./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY) $(ARGS)

run:
	@echo "Running sparkle-duck (debug)..."
	@./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(MAIN_BINARY) -b wayland $(ARGS)

run-release: release
	@echo "Running sparkle-duck (release)..."
	@./$(BUILD_RELEASE_DIR)/$(BIN_DIR)/$(MAIN_BINARY) -b wayland $(ARGS)

run-asan: asan
	@echo "Running sparkle-duck with AddressSanitizer..."
	@echo "ASAN will report memory errors as they occur."
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:print_stats=1 ./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(MAIN_BINARY) -b wayland $(ARGS)

format:
	@echo "Formatting source code..."
	@find src -name "*.cpp" -o -name "*.h" | xargs clang-format --style=file -i

lint:
	@echo "Linting JavaScript..."
	@if [ ! -d "node_modules" ]; then \
		echo "Installing npm dependencies..."; \
		npm install; \
	fi
	@npm run lint

lint-fix:
	@echo "Linting and fixing JavaScript..."
	@if [ ! -d "node_modules" ]; then \
		echo "Installing npm dependencies..."; \
		npm install; \
	fi
	@npm run lint:fix

format-check:
	@echo "Checking C++ formatting..."
	@NEEDS_FORMAT=$$(find src -name "*.cpp" -o -name "*.h" | xargs clang-format --style=file --dry-run -Werror 2>&1) || { \
		echo "$$NEEDS_FORMAT"; \
		echo ""; \
		echo "❌ Some files need formatting. Run 'make format' to fix."; \
		exit 1; \
	}
	@echo "✅ C++ formatting OK"

check: format-check lint build-tests
	@echo "Running unit tests..."
	@./$(BUILD_DEBUG_DIR)/$(BIN_DIR)/$(TEST_BINARY) --gtest_brief=1
	@echo ""
	@echo "✅ All checks passed!"

# Cross-compilation targets (build for aarch64 on x86_64 host).
cross-sysroot:
	@echo "Syncing sysroot from Pi..."
	@./cmake/aarch64-sysroot-sync.sh

cross-debug:
	@echo "Cross-compiling debug version for aarch64..."
	@if [ ! -d "sysroot-aarch64" ]; then \
		echo "Error: sysroot-aarch64 not found. Run 'make cross-sysroot' first."; \
		exit 1; \
	fi
	$(call start_build)
	@cmake -B $(BUILD_CROSS_DEBUG_DIR) -S . \
		-DCMAKE_TOOLCHAIN_FILE=$(CROSS_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Debug
	@$(MAKE) -C $(BUILD_CROSS_DEBUG_DIR) -j$(JOBS)
	$(call show_build_stats)

cross-release:
	@echo "Cross-compiling release version for aarch64..."
	@if [ ! -d "sysroot-aarch64" ]; then \
		echo "Error: sysroot-aarch64 not found. Run 'make cross-sysroot' first."; \
		exit 1; \
	fi
	$(call start_build)
	@cmake -B $(BUILD_CROSS_RELEASE_DIR) -S . \
		-DCMAKE_TOOLCHAIN_FILE=$(CROSS_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release
	@$(MAKE) -C $(BUILD_CROSS_RELEASE_DIR) -j$(JOBS)
	$(call show_build_stats)

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DEBUG_DIR) $(BUILD_RELEASE_DIR) $(BUILD_CROSS_DEBUG_DIR) $(BUILD_CROSS_RELEASE_DIR) $(BIN_DIR) lib
	@echo "Note: To clean in-source CMake artifacts, run: make clean-cmake"

clean-cmake:
	@echo "Cleaning in-source CMake artifacts..."
	@rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake CTestTestfile.cmake Testing

help:
	@echo "Available targets:"
	@echo "  all            - Build release version (default)"
	@echo "  release        - Build optimized release version"
	@echo "  release-verify - Build release and run tests to verify"
	@echo "  debug          - Build debug version with symbols and LOG_DEBUG"
	@echo "  asan           - Build debug version with AddressSanitizer enabled"
	@echo "  build-tests    - Build test binary without running tests"
	@echo "  build-tests-slow - Build slow test binary"
	@echo "  test           - Build debug and run unit tests (fast)"
	@echo "  test-slow      - Run slow integration/stress tests"
	@echo "  test-asan      - Build and run tests with AddressSanitizer"
	@echo "  test-all       - Run all tests (unit + slow + visual)"
	@echo "  visual-tests   - Run tests with visual output enabled"
	@echo "  run            - Run the main sparkle-duck executable"
	@echo "  run-asan       - Run with AddressSanitizer to detect memory errors"
	@echo "  format         - Format C++ source code using clang-format"
	@echo "  format-check   - Check C++ formatting without modifying"
	@echo "  lint           - Lint JavaScript files with ESLint"
	@echo "  lint-fix       - Lint and auto-fix JavaScript files"
	@echo "  check          - Run all checks (format-check, lint, tests) - used by CI"
	@echo "  clean          - Remove build directory and binaries"
	@echo "  clean-cmake    - Remove in-source CMake artifacts"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Cross-compilation (build on x86_64, deploy to aarch64 Pi):"
	@echo "  cross-sysroot  - Sync libraries from Pi to local sysroot"
	@echo "  cross-debug    - Cross-compile debug version for aarch64"
	@echo "  cross-release  - Cross-compile release version for aarch64"
	@echo ""
	@echo "Pass arguments to test/run targets using ARGS:"
	@echo "  make test ARGS='--gtest_filter=WorldB*'"
	@echo "  make run ARGS='-b sdl -s 100'"
	@echo ""
	@echo "For debugging memory issues:"
	@echo "  make run-asan ARGS='-b sdl -s 0'  # Run with ASAN"
	@echo "  make test-asan ARGS='--gtest_filter=*ScenarioSwitch*'"