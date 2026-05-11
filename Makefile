# ============================================================================
# C-Neural-Engine — Makefile
# Zero-dependency deep learning framework in C99
# ============================================================================

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -Werror -pedantic -g -O2
INCLUDES = -Iinclude

# Source files
SRC_DIR   = src
TEST_DIR  = tests
INC_DIR   = include
EXAMPLE_DIR = examples

SRCS = $(SRC_DIR)/tensor.c \
       $(SRC_DIR)/activation.c \
       $(SRC_DIR)/loss.c \
       $(SRC_DIR)/layer.c \
       $(SRC_DIR)/network.c \
       $(SRC_DIR)/optimizer.c

OBJS = $(SRCS:.c=.o)

# Test files
TEST_SRCS = $(TEST_DIR)/test_main.c \
            $(TEST_DIR)/test_tensor.c \
            $(TEST_DIR)/test_activation.c \
            $(TEST_DIR)/test_loss.c \
            $(TEST_DIR)/test_layer.c \
            $(TEST_DIR)/test_network.c \
            $(TEST_DIR)/test_optimizer.c
TEST_BIN  = test_runner

# Example binaries
XOR_BIN       = xor_example
BENCH_BIN     = benchmark
GRADCHECK_BIN = gradient_check
MKTGEN_BIN    = market_gen_bin
DQN_BIN       = dqn_trader_bin

# ============================================================================
# Targets
# ============================================================================

.PHONY: all test clean memcheck examples bench gradcheck market_gen dqn wasm wasm-clean

all: $(OBJS)

# Compile object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/%.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ---- Tests ----
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- Examples ----
examples: $(XOR_BIN)
	./$(XOR_BIN)

$(XOR_BIN): $(SRCS) $(EXAMPLE_DIR)/xor.c
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- Benchmark ----
bench: $(BENCH_BIN)
	./$(BENCH_BIN)

$(BENCH_BIN): $(SRCS) $(EXAMPLE_DIR)/benchmark.c
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- Gradient Check ----
gradcheck: $(GRADCHECK_BIN)
	./$(GRADCHECK_BIN)

$(GRADCHECK_BIN): $(SRCS) $(EXAMPLE_DIR)/gradient_check.c
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- Market Data Generator ----
market_gen: $(MKTGEN_BIN)
	./$(MKTGEN_BIN)

$(MKTGEN_BIN): $(SRCS) $(EXAMPLE_DIR)/market_generator.c
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- DQN Trading Agent ----
dqn: $(DQN_BIN)
	./$(DQN_BIN)

$(DQN_BIN): $(SRCS) $(EXAMPLE_DIR)/dqn_trader.c
	$(CC) $(CFLAGS) $(INCLUDES) -lm $^ -o $@

# ---- Memory Check (Valgrind or LeakSanitizer) ----
memcheck: $(TEST_BIN)
	@if command -v valgrind > /dev/null 2>&1; then \
		valgrind --leak-check=full --error-exitcode=1 ./$(TEST_BIN); \
	else \
		echo "Valgrind not found. Rebuilding with AddressSanitizer..."; \
		$(CC) $(CFLAGS) $(INCLUDES) -fsanitize=address -lm $(SRCS) $(TEST_SRCS) -o $(TEST_BIN)_asan; \
		./$(TEST_BIN)_asan; \
		rm -f $(TEST_BIN)_asan; \
	fi

# ============================================================================
# WebAssembly builds (Emscripten) — powers the in-browser demo at web/
# ============================================================================

EMCC       = emcc
WASM_DIR   = web/wasm
EMCC_FLAGS = -O2 -Iinclude \
             -sMODULARIZE=1 \
             -sENVIRONMENT=web,worker \
             -sALLOW_MEMORY_GROWTH=1 \
             -sINITIAL_MEMORY=33554432 \
             -sINVOKE_RUN=0 \
             -sEXPORTED_RUNTIME_METHODS=callMain \
             -sEXIT_RUNTIME=1

.PHONY: wasm wasm-clean

wasm: $(WASM_DIR)/xor.js \
      $(WASM_DIR)/dqn.js \
      $(WASM_DIR)/market_gen.js \
      $(WASM_DIR)/benchmark.js \
      $(WASM_DIR)/gradient_check.js

$(WASM_DIR)/xor.js: $(SRCS) $(EXAMPLE_DIR)/xor.c
	@mkdir -p $(WASM_DIR)
	$(EMCC) $(EMCC_FLAGS) -sEXPORT_NAME=createXor $^ -o $@

$(WASM_DIR)/dqn.js: $(SRCS) $(EXAMPLE_DIR)/dqn_trader.c
	@mkdir -p $(WASM_DIR)
	$(EMCC) $(EMCC_FLAGS) -sEXPORT_NAME=createDqn $^ -o $@

$(WASM_DIR)/market_gen.js: $(SRCS) $(EXAMPLE_DIR)/market_generator.c
	@mkdir -p $(WASM_DIR)
	$(EMCC) $(EMCC_FLAGS) -sEXPORT_NAME=createMarketGen $^ -o $@

$(WASM_DIR)/benchmark.js: $(SRCS) $(EXAMPLE_DIR)/benchmark.c
	@mkdir -p $(WASM_DIR)
	$(EMCC) $(EMCC_FLAGS) -sEXPORT_NAME=createBenchmark $^ -o $@

$(WASM_DIR)/gradient_check.js: $(SRCS) $(EXAMPLE_DIR)/gradient_check.c
	@mkdir -p $(WASM_DIR)
	$(EMCC) $(EMCC_FLAGS) -sEXPORT_NAME=createGradientCheck $^ -o $@

wasm-clean:
	rm -rf $(WASM_DIR)

# ---- Clean ----
clean:
	rm -f $(SRC_DIR)/*.o $(TEST_BIN) $(TEST_BIN)_asan $(XOR_BIN) $(BENCH_BIN) $(GRADCHECK_BIN) $(MKTGEN_BIN) $(DQN_BIN)
