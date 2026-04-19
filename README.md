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

See [`Module 2/README.md`](Module%202/README.md) for full algorithm description, build instructions, usage, and all benchmark results.

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
