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

---

## 📂 Repository Structure

This repository is organized into multiple modules:
