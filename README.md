# SIMD-optimized-partitioned-hash-join

# 📌 Project Overview

This project implements a **Partitioned Hash Join with Duplicates**, a fundamental operation used in **data analytics systems and database engines**.

In an **equi-join**, the goal is to find all pairs of records:

(r ∈ R, s ∈ S) such that r.key = s.key

This must work efficiently even when the same key appears multiple times in either dataset.

---

## ⚙️ Parallelization Strategy

To efficiently process large datasets, the problem is divided into **independent subproblems** using partitioning:

- Each record is assigned to a **partition** based on its key  
- A **hash function** maps each key to a partition ID  
- Records with the same partition ID are grouped together  
- Each partition can be processed **independently and in parallel**

After partitioning:
- A lookup structure is built for each partition of **R**
- The corresponding partition of **S** is probed against it

---

## 🚀 Critical Kernel: Partition Mapping

The first and most important step in this pipeline is:

**Mapping each 64-bit key to a partition identifier in the range [0, P)**

This operation:

- Is applied to a very large number of keys (streaming kernel)  
- Runs over the entire dataset  
- Lies on the critical execution path  

Because of this, its performance has a **direct impact on the overall execution time**.

---

## 🧩 Module 1: Vectorization of the Partition Mapping Kernel

Module 1 focuses on optimizing this key operation:

- Efficient mapping of 64-bit keys to partition IDs  
- Implementation in **C++**  
- Optimization using:
  - GCC auto-vectorization  
  - AVX2 SIMD intrinsics  

The goal is to:

- Improve throughput  
- Compare scalar vs vectorized implementations  
- Analyze performance limitations (e.g., memory-bound behavior)

See [`Module 1/README.md`](Module%201/README.md) for full details.

---

## 🧩 Module 2: Parallel Partitioned Hash Join with Duplicates

Module 2 builds on the partition mapping from Module 1 and implements a **complete, correct, and benchmarked hash join pipeline**:

- Both a **sequential** reference and a **parallel** (`std::thread`) implementation  
- Correct handling of **duplicate keys** (multiplicity-aware build/probe)  
- Comprehensive **strong scalability**, **weak scalability**, and **partition-count** experiments  
- Tested on the **SPM cluster (node09)**

Key results (NR = NS = 10M records, P = 256 partitions):

| Version | Time (s) | Speedup |
|---|---|---|
| Sequential | 0.689 | 1.00× |
| Parallel (8 threads) | 0.177 | **3.91×** |

See [`Module 2/README.md`](Module%202/README.md) for the full algorithm description and all benchmark results.

---

## 🚀 Quick Start — Run the Full Pipeline (Module 2)

> Module 2 is the complete pipeline — it uses the partition mapping from Module 1 and delivers a fully working parallel hash join.

### Requirements

| Requirement | Detail |
|---|---|
| Compiler | `g++` with C++20 support |
| Architecture | x86-64 with AVX2 |
| OS | Linux |
| Threads | POSIX threads (`-pthread`) |

### 1. Clone the repository

```bash
git clone https://github.com/hamasli/SIMD-optimized-partitioned-hash-join.git
cd SIMD-optimized-partitioned-hash-join/Module\ 2
```

### 2. Build

```bash
# Build both sequential and parallel binaries
make all

# Or build individually
make seq       # sequential only
make par       # parallel only
make clean     # remove compiled binaries
```

### 3. Run correctness tests

```bash
make test-small          # tiny input (NR=10, NS=10) — naive verifier triggered
make test-medium-small   # NR=200, NS=200 — high duplicate rate
make test-medium         # NR=1M, NS=1M — correctness + first speedup check
```

Expected output: `naive_check=PASS` and `par_vs_seq_check=PASS`

### 4. Run the main benchmark

```bash
make test-large          # NR=10M, NS=10M, P=256, T=8
```

Override parameters from the command line:

```bash
make test-large NR=50000000 NS=50000000 P=512 T=16
```

### 5. Scalability experiments

```bash
make run-strong    # strong scalability — fixed NR=NS=20M, threads: 1,2,4,8,16
make run-weak      # weak scalability  — NR/NS grows with thread count
make run-vary-p    # vary partition count P — fixed NR=NS=10M, T=8
```

### 6. Run binaries directly

```bash
./bin/hashjoin_seq -nr 10000000 -ns 10000000 -seed 42 -max-key 1000000 -p 256
./bin/hashjoin_par -nr 10000000 -ns 10000000 -seed 42 -max-key 1000000 -p 256 -t 8
```

| Flag | Description | Default |
|---|---|---|
| `-nr` | Records in relation R | `10000000` |
| `-ns` | Records in relation S | `10000000` |
| `-seed` | Random seed | `42` |
| `-max-key` | Key range `[0, MAX_KEY)` — controls duplicate rate | `1000000` |
| `-p` | Number of partitions (must be power of two) | `256` |
| `-t` | Number of threads *(parallel binary only)* | `8` |

---

## 📂 Repository Structure

This repository is organized into multiple modules:

```
SIMD-optimized-partitioned-hash-join/
├── Module 1/          # SIMD-optimized partition mapping kernel (AVX2)
│   ├── src/
│   ├── build/
│   ├── reports/
│   ├── Makefile
│   └── README.md
├── Module 2/          # Parallel partitioned hash join (std::thread)
│   ├── src/
│   ├── bin/
│   ├── reports/
│   ├── Makefile
│   └── README.md
└── README.md          # This file — project overview
```

---

## 👤 Author

**Hamas Ali** — Student ID: 726267  
MSc Computer Science — University of Pisa  
Course: SPM (Scalable and Parallel Methodologies)
