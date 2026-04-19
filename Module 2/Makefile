

# This Makefile builds and tests both the sequential and parallel

# USAGE:
#   make all          -> build both binaries
#   make seq          -> build sequential only
#   make par          -> build parallel only
#   make test-small   -> correctness check on small input
#   make test-medium  -> correctness check on medium input
#   make test-large   -> performance benchmark on large input
#   make run-strong   -> strong scalability experiment (fixed N, vary threads)
#   make run-weak     -> weak scalability experiment (N grows with threads)
#   make clean        -> remove all compiled binaries
#   make run-vary-p   -> increse the partiontion P only and  check the behaviour
# =============================================================================

CXX      := g++


CXXFLAGS := -O3 -std=c++20 -Wall -Wextra -pthread -mavx2


# Source files and output binaries

SEQ_SRC  := src/hashjoin_seq.cpp   # Sequential reference implementation
PAR_SRC  := src/hashjoin_par.cpp   # Parallel implementation with C++ threads

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

BIN_DIR  := bin
SEQ_BIN  := $(BIN_DIR)/hashjoin_seq
PAR_BIN  := $(BIN_DIR)/hashjoin_par


#  Default benchmark parameters
# These can be overridden from the command line, e.g.:
#  make test-large NR=50000000 NS=50000000 P=512 T=16

NR      := 10000000   # Number of records in relation R
NS      := 10000000   # Number of records in relation S
SEED    := 42         # Random seed (deterministic, reproducible)
MAX_KEY := 1000000    # Keys drawn from [0, MAX_KEY), controls duplicate rate
P       := 256        # Number of partitions (must be power of two)
T       := 8          # Default number of threads for parallel runs



# Build both binaries (default target)
.PHONY: all
all: $(SEQ_BIN) $(PAR_BIN)
	@echo ""
	@echo "Build complete:"
	@echo "  Sequential : ./$(SEQ_BIN)"
	@echo "  Parallel   : ./$(PAR_BIN)"


# Build the sequential binary from hashjoin_seq.cpp
$(SEQ_BIN): $(SEQ_SRC)  | $(BIN_DIR)
	@echo "Compiling sequential implementation..."
	$(CXX) $(CXXFLAGS) -o $@ $<
	@echo "  -> $(SEQ_BIN) built successfully"


# Build the parallel binary from hashjoin_par.cpp
$(PAR_BIN): $(PAR_SRC)  | $(BIN_DIR)
	@echo "Compiling parallel implementation..."
	$(CXX) $(CXXFLAGS) -o $@ $<
	@echo "  -> $(PAR_BIN) built successfully"


# Convenience targets to build one at a time
.PHONY: seq
seq: $(SEQ_BIN)

.PHONY: par
par: $(PAR_BIN)


# CORRECTNESS TESTS

# Small correctness test
# NR=10, NS=10: tiny input, naive brute-force verifier is triggered.
# Both sequential and parallel results are compared against the naive result.
# The parallel output is also compared against the sequential output.
# Expected output: naive_check=PASS and par_vs_seq_check=PASS


.PHONY: test-small
test-small: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "SMALL CORRECTNESS TEST"
	@echo "NR=10  NS=10  P=4  seed=13  max-key=8"
	@echo "Naive brute-force verifier will be triggered."
	@echo "=============================================="
	@echo ""
	@echo "--- Sequential ---"
	./$(SEQ_BIN) -nr 10 -ns 10 -seed 13 -max-key 8 -p 4
	@echo ""
	@echo "--- Parallel (threads=4) ---"
	./$(PAR_BIN) -nr 10 -ns 10 -seed 13 -max-key 8 -p 4 -t 4
	@echo ""
	@echo "EXPECTED: naive_check=PASS and par_vs_seq_check=PASS"



# Medium correctness test
# NR=200, NS=200: still small enough for naive verifier.
# Tests that duplicates are counted correctly.
.PHONY: test-medium-small
test-medium-small: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "MEDIUM-SMALL CORRECTNESS TEST"
	@echo "NR=200  NS=200  P=8  seed=42  max-key=20"
	@echo "High duplicate rate (200 records, only 20 keys)."
	@echo "=============================================="
	@echo ""
	@echo "--- Sequential ---"
	./$(SEQ_BIN) -nr 200 -ns 200 -seed 42 -max-key 20 -p 8
	@echo ""
	@echo "--- Parallel (threads=4) ---"
	./$(PAR_BIN) -nr 200 -ns 200 -seed 42 -max-key 20 -p 8 -t 4
	@echo ""
	@echo "EXPECTED: naive_check=PASS and par_vs_seq_check=PASS"



# Medium correctness + performance test
# NR=1M, NS=1M: large enough to see parallelism benefits.
# No naive verifier (too slow for this size).
# Sequential and parallel checksums must match.

.PHONY: test-medium
test-medium: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "MEDIUM CORRECTNESS + PERFORMANCE TEST"
	@echo "NR=1000000  NS=1000000  P=128  seed=42"
	@echo "=============================================="
	@echo ""
	@echo "--- Sequential ---"
	./$(SEQ_BIN) -nr 1000000 -ns 1000000 -seed 42 -max-key 100000 -p 128
	@echo ""
	@echo "--- Parallel (threads=$(T)) ---"
	./$(PAR_BIN) -nr 1000000 -ns 1000000 -seed 42 -max-key 100000 -p 128 -t $(T)
	@echo ""
	@echo "EXPECTED: par_vs_seq_check=PASS, speedup > 1"



# Large performance benchmark
# NR=10M, NS=10M: main benchmark used in the report.
# Measures throughput and speedup vs sequential baseline.
# -----------------------------------------------------------------------------
.PHONY: test-large
test-large: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "LARGE PERFORMANCE BENCHMARK"
	@echo "NR=$(NR)  NS=$(NS)  P=$(P)  seed=$(SEED)  max-key=$(MAX_KEY)"
	@echo "=============================================="
	@echo ""
	@echo "--- Sequential baseline ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p $(P)
	@echo ""
	@echo "--- Parallel (threads=$(T)) ---"
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t $(T)
	@echo ""
	@echo "EXPECTED: par_vs_seq_check=PASS, speedup > 1"



# SCALABILITY EXPERIMENTS

# -----------------------------------------------------------------------------
# Strong scalability experiment
# Fixed problem size: NR=20M, NS=20M
# Vary number of threads: 1, 2, 4, 8, 16
# Good strong scalability means time halves when threads double.
# The sequential result is printed first as the baseline.

.PHONY: run-strong
run-strong: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "STRONG SCALABILITY EXPERIMENT"
	@echo "Fixed size: NR=20000000  NS=20000000"
	@echo "Varying threads: 1, 2, 4, 8, 16"
	@echo "=============================================="
	@echo ""
	@echo "--- Sequential baseline ---"
	./$(SEQ_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P)
	@echo ""
	@echo "--- Parallel: 1 thread ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 1
	@echo ""
	@echo "--- Parallel: 2 threads ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 2
	@echo ""
	@echo "--- Parallel: 4 threads ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 4
	@echo ""
	@echo "--- Parallel: 8 threads ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 8
	@echo ""
	@echo "--- Parallel: 16 threads ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 16
	@echo ""
	@echo "Strong scalability done. Check speedup values above."



# Weak scalability experiment
# Both problem size and thread count grow together.
# Base: NR=5M per thread.
# If time stays roughly constant as threads increase -> good weak scalability.
#   1 thread  -> NR =  5M
#   2 threads -> NR = 10M
#   4 threads -> NR = 20M
#   8 threads -> NR = 40M
#  16 threads -> NR = 80M

.PHONY: run-weak
run-weak: $(PAR_BIN)
	@echo "=============================================="
	@echo "WEAK SCALABILITY EXPERIMENT"
	@echo "Problem size grows with thread count."
	@echo "Base: NR = NS = 5000000 per thread"
	@echo "=============================================="
	@echo ""
	@echo "--- 1 thread  | NR=5000000  NS=5000000 ---"
	./$(PAR_BIN) -nr 5000000 -ns 5000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 1
	@echo ""
	@echo "--- 2 threads | NR=10000000 NS=10000000 ---"
	./$(PAR_BIN) -nr 10000000 -ns 10000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 2
	@echo ""
	@echo "--- 4 threads | NR=20000000 NS=20000000 ---"
	./$(PAR_BIN) -nr 20000000 -ns 20000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 4
	@echo ""
	@echo "--- 8 threads | NR=40000000 NS=40000000 ---"
	./$(PAR_BIN) -nr 40000000 -ns 40000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 8
	@echo ""
	@echo "--- 16 threads | NR=80000000 NS=80000000 ---"
	./$(PAR_BIN) -nr 80000000 -ns 80000000 -seed $(SEED) -max-key $(MAX_KEY) -p $(P) -t 16
	@echo ""
	@echo "Weak scalability done. Time should stay roughly constant."



# EXTRA: Vary P (partition count) experiment
# Keeps NR, NS, T fixed but changes P.
# Shows how partition count affects performance.

.PHONY: run-vary-p
run-vary-p: $(SEQ_BIN) $(PAR_BIN)
	@echo "=============================================="
	@echo "VARY P EXPERIMENT"
	@echo "NR=$(NR)  NS=$(NS)  threads=$(T)  seed=$(SEED)"
	@echo "Varying P: 64, 128, 256, 512, 1024"
	@echo "=============================================="
	@echo ""
	@echo "--- P=64 ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 64
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 64  -t $(T)
	@echo ""
	@echo "--- P=128 ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 128
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 128 -t $(T)
	@echo ""
	@echo "--- P=256 ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 256
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 256 -t $(T)
	@echo ""
	@echo "--- P=512 ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 512
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 512 -t $(T)
	@echo ""
	@echo "--- P=1024 ---"
	./$(SEQ_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 1024
	./$(PAR_BIN) -nr $(NR) -ns $(NS) -seed $(SEED) -max-key $(MAX_KEY) -p 1024 -t $(T)



# CLEAN

# Remove all compiled binaries
.PHONY: clean
clean:
	@echo "Removing compiled binaries..."
	rm -f $(SEQ_BIN) $(PAR_BIN)
	@echo "Clean done."
