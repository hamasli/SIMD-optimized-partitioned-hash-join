# Partitioned Hash Join with Duplicates — Module 2

**Course:** SPM — Parallel and Distributed Systems  
**Author:** Hamas Ali — Student ID: 726267  
**University of Pisa**

> **Full project repository:** [SIMD-optimized-partitioned-hash-join](https://github.com/hamasli/SIMD-optimized-partitioned-hash-join)  
> **Module 1** (SIMD-optimized hash-based partition mapping) lives in the `module-1/` folder of the same repository.

---

## About This Project

This is a two-module master's project for the SPM (Scalable and Parallel Methodologies) course at the University of Pisa.

| Module | Topic | Key Technology |
|--------|-------|----------------|
| **Module 1** | SIMD-optimized hash-based partition mapping | AVX2 intrinsics, vectorized hash computation |
| **Module 2** *(this folder)* | Parallel partitioned hash join with duplicate support | `std::thread`, C++20, multi-phase parallelism |

Both modules build on the same core idea: **partitioned hash joins** are a fundamental building block in high-performance database systems. Module 1 established an efficient, vectorized partition mapping; Module 2 uses that foundation to implement a complete, correct, and benchmarked join pipeline.

---

## Overview

This module implements a **partitioned hash join with duplicate support** in C++. Module 1 focused on designing an efficient hash-based partition mapping; this module integrates that mapping into a complete join pipeline, implemented in two versions:

- **Sequential** — the correctness reference and performance baseline
- **Parallel** — a multi-threaded version using C++ standard threads (`std::thread`)

The algorithm partitions both input relations using the same hash mapping, stores each partition contiguously in memory, and then performs an independent join per partition. Duplicate keys are handled correctly by tracking key multiplicity during the build phase.

All experiments reported were executed on **node09 of the SPM cluster**. Raw output files are available in the `reports/` directory.

---

## Project Structure

```
.
├── src/
│   ├── hashjoin_seq.cpp      # Sequential reference implementation
│   └── hashjoin_par.cpp      # Parallel implementation (C++ threads)
├── bin/
│   ├── hashjoin_seq          # Compiled sequential binary
│   └── hashjoin_par          # Compiled parallel binary
├── reports/
│   ├── small.txt             # Small correctness test output
│   ├── medium.txt            # Medium benchmark output
│   ├── large.txt             # Large benchmark output
│   ├── strong.txt            # Strong scalability experiment output
│   ├── weak.txt              # Weak scalability experiment output
│   └── vary_p.txt            # Partition count variation output
├── Makefile
└── README.md
```

---

## Algorithm

The join pipeline has three main phases:

### 1. Partitioning Phase
Each relation is partitioned in three steps:
1. **Histogram** — count how many records map to each partition
2. **Prefix sum** — compute starting offsets for each partition in the output array
3. **Scatter** — rearrange records so each partition occupies a contiguous memory region

### 2. Hash Function
Keys are mapped to partitions using a folded multiply-shift scheme:
- XOR the upper and lower 32 bits of the 64-bit key (both halves influence the result)
- Multiply by Knuth's multiplicative constant to improve distribution
- Extract the partition ID via a shift-and-mask (number of partitions `P` must be a power of two)

### 3. Join Phase
For each partition pair `(R_i, S_i)`:
- Build a hash table on `R_i`, recording the multiplicity of each key
- Probe with `S_i`: for each key found, add its multiplicity in `R` to the result count

---

## Requirements

| Requirement | Version |
|---|---|
| C++ Standard | C++20 |
| Compiler | `g++` (GCC recommended) |
| Architecture | x86-64 with AVX2 support |
| Threads | POSIX threads (`-pthread`) |
| OS | Linux (tested on Ubuntu / SPM cluster) |

---

## Build

```bash
# Build both sequential and parallel binaries
make all

# Build sequential only
make seq

# Build parallel only
make par

# Remove compiled binaries
make clean
```

Binaries are placed in the `bin/` directory after compilation.

---

## Usage

Both binaries accept the same command-line parameters:

```
./bin/hashjoin_seq -nr <N> -ns <N> -seed <S> -max-key <K> -p <P>
./bin/hashjoin_par -nr <N> -ns <N> -seed <S> -max-key <K> -p <P> -t <T>
```

| Flag | Description | Default |
|---|---|---|
| `-nr` | Number of records in relation R | `10000000` |
| `-ns` | Number of records in relation S | `10000000` |
| `-seed` | Random seed (for reproducibility) | `42` |
| `-max-key` | Keys drawn from `[0, MAX_KEY)` — controls duplicate rate | `1000000` |
| `-p` | Number of partitions (must be a power of two) | `256` |
| `-t` | Number of threads *(parallel binary only)* | `8` |

---

## Running Tests and Experiments

All Makefile targets below build the binaries if they are not already compiled.

### Correctness Tests

```bash
# Tiny input (NR=10, NS=10, P=4) — triggers naive brute-force verifier
make test-small

# Medium-small input (NR=200, NS=200, P=8) — high duplicate rate
make test-medium-small

# Medium input (NR=1M, NS=1M, P=128) — correctness + first performance check
make test-medium
```

Expected output for all correctness tests: `naive_check=PASS` and `par_vs_seq_check=PASS`.

### Performance Benchmark

```bash
# Large benchmark (NR=10M, NS=10M, P=256, T=8) — main benchmark from the report
make test-large
```

Override any parameter from the command line:
```bash
make test-large NR=50000000 NS=50000000 P=512 T=16
```

### Scalability Experiments

```bash
# Strong scalability — fixed NR=NS=20M, threads: 1, 2, 4, 8, 16
make run-strong

# Weak scalability — NR=NS grows with thread count (5M records per thread)
make run-weak

# Partition count variation — fixed NR=NS=10M, T=8, P: 64, 128, 256, 512, 1024
make run-vary-p
```

---

## Performance Results (node09, SPM Cluster)

### Large Dataset — NR = NS = 10M, P = 256

| Version | Time (s) | Speedup |
|---|---|---|
| Sequential | 0.689 | 1.00× |
| Parallel (8 threads) | 0.177 | **3.91×** |

### Phase Breakdown (Large Dataset)

| Phase | Sequential (s) | Parallel (s) |
|---|---|---|
| Histogram R | 0.0509 | 0.0076 |
| Prefix / Offset R | 0.000003 | 0.000011 |
| Scatter R | 0.1288 | 0.0575 |
| Histogram S | 0.0518 | 0.0088 |
| Prefix / Offset S | 0.000003 | 0.000014 |
| Scatter S | 0.1287 | 0.0572 |
| Join | 0.3290 | 0.0456 |

Histogram, scatter, and join all benefit substantially from parallelism. The prefix/offset phase is negligible in both versions.

### Strong Scalability — NR = NS = 20M, P = 256

| Threads | Time (s) | Speedup |
|---|---|---|
| 1 | 1.356 | 1.01× |
| 2 | 0.791 | 1.73× |
| 4 | 0.488 | 2.91× |
| 8 | 0.343 | 3.96× |
| 16 | 0.315 | 4.35× |

### Weak Scalability — 5M records per thread

| Threads | NR = NS | Time (s) |
|---|---|---|
| 1 | 5M | 0.422 |
| 2 | 10M | 0.405 |
| 4 | 20M | 0.492 |
| 8 | 40M | 0.677 |
| 16 | 80M | 1.234 |

### Effect of Partition Count — NR = NS = 10M, T = 8

| P | Sequential (s) | Parallel (s) | Speedup |
|---|---|---|---|
| 64 | 0.824 | 0.205 | 4.18× |
| 128 | 0.774 | 0.190 | 4.02× |
| 256 | 0.683 | 0.177 | 4.04× |
| 512 | 0.651 | 0.169 | 4.09× |
| 1024 | 0.631 | 0.175 | 3.66× |

Best performance is achieved in the range **P = 256 to P = 512**.

---

## Correctness Verification

Two verification strategies are used:

- **Small inputs** — results are compared against a naive O(N²) brute-force join. Three values must match: `join_count`, `checksum1`, `checksum2`.
- **Large inputs** — the parallel output is compared directly against the sequential reference. All experiments reported in `reports/` pass this check.

Correctness checks run outside the measured region and do not affect timing.

---

## Parallelization Design

| Phase | Strategy |
|---|---|
| Histogram | Each thread builds a private local histogram → merged after |
| Prefix sum & offsets | Kept sequential (negligible cost) |
| Scatter | Pre-computed per-thread offsets → lock-free parallel writes |
| Join | Partitions are independent → each thread processes a range |

The design avoids global locking during scatter and join. Parallelism is applied only where experimental evidence shows it is beneficial.

---

## License

This project was developed for academic purposes as part of the SPM course at the University of Pisa. Not licensed for redistribution.
