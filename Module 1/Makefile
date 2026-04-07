CXX := g++
SRC := src/main.cpp

# Default flags
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -Wpedantic

# Safer common target for both login node and node09
ARCHFLAGS := -mavx2

# Baseline: vectorization disabled
BASEFLAGS := $(CXXFLAGS) $(ARCHFLAGS) -fno-tree-vectorize

# Auto-vectorized: vectorization enabled + reports

AUTOVECFLAGS := $(CXXFLAGS) $(ARCHFLAGS) -fopt-info-vec-all=reports/vectorization_report.txt
BIN_DIR := build
REPORT_DIR := reports

BASELINE := $(BIN_DIR)/part_baseline
AUTOVEC := $(BIN_DIR)/part_autovec

AVX2 := build/part_avx2

avx2:
	$(CXX) -O3 -mavx2 src/mapper_avx2.cpp -o $(AVX2)

.PHONY: all baseline autovec clean run-small run-large report dirs

all: dirs baseline autovec

dirs:
	mkdir -p $(BIN_DIR) $(REPORT_DIR)

baseline: dirs
	$(CXX) $(BASEFLAGS) $(SRC) -o $(BASELINE)

autovec: dirs
	$(CXX) $(AUTOVECFLAGS) $(SRC) -o $(AUTOVEC)

run-small: all
	$(BASELINE) --N 16 --P 8 --seed 42 --small-check --print-sample
	$(AUTOVEC) --N 16 --P 8 --seed 42 --small-check --print-sample

run-large: all
	$(BASELINE) --N 50000000 --P 256 --seed 42 --repeats 7
	$(AUTOVEC) --N 50000000 --P 256 --seed 42 --repeats 7
run-avx2: all avx2
	./build/part_avx2 --N 16 --P 8 --seed 42 --small-check --print-sample | tee reports/avx2_small.txt
	./build/part_avx2 --N 50000000 --P 256 --seed 42 --repeats 7

report:
	@echo "==== Optimized vectorization report ===="
	@cat $(REPORT_DIR)/vectorization_report.txt || true
	
clean:
	rm -rf $(BIN_DIR) $(REPORT_DIR)
