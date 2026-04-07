# SPM Module 1 – Vectorization of Partition Mapping Kernel

##  Project Overview

This project implements a **partitioned hash join mapping kernel**, which is a key component in data analytics systems.

In an equi-join, we need to find all pairs of records between the R and S table dataset such that:

[
r.key = s.key
]

This must work even when duplicate keys exist in the input data.

To parallelize the problem efficiently, the input is divided into **independent partitions**. Each key is mapped to a partition ID using a hash function. All keys that map to the same partition are grouped together, enabling independent processing of each partition.

The focus of this module is the **partition mapping step**, where:

* Input: array of 64-bit keys
* Output: array of partition IDs in ([0, P))
* This operation is applied to large datasets and lies on the critical path

---

##  Mapping Function Design

The partition mapping is implemented using a **deterministic multiplicative hash function** designed for high performance and good distribution.

Each 64-bit key is first reduced to 32 bits by combining its upper and lower halves using a bitwise XOR. This folding step ensures that information from the entire key contributes to the final hash value while keeping the computation efficient.

The resulting 32-bit value is then multiplied by a constant (Knuth’s multiplicative constant), which helps in uniformly spreading the keys across the available partitions.

Finally, the partition ID is computed using:

* a right shift operation to extract the most significant bits
* a masking operation to ensure the result lies in the range ([0, P))

###  Why this design?

* **Efficiency**:
  Uses only simple operations (XOR, multiplication, shift, mask), which are very fast and SIMD-friendly.

* **Good distribution**:
  The multiplicative hash reduces clustering and distributes keys evenly across partitions.

* **Power-of-two optimization**:
  Since (P) is chosen as a power of two, modulo operations are replaced by a fast bitmask.

* **Vectorization-friendly**:
  The computation is independent per element and avoids branches, making it ideal for:

  * compiler auto-vectorization
  * manual SIMD (AVX2) implementation

* **Deterministic behavior**:
  Given the same input and seed, the mapping always produces the same output, which is important for reproducibility.

---
##  Project Structure

The project is organized as follows:

```
module_1/
│
├── src/
│   ├── main.cpp              # Task 1 (baseline & autovec)
│   └── mapper_avx2.cpp       # Task 2
│
├── build/                    # Compiled binaries
│   ├── part_baseline
│   ├── part_autovec
│   └── part_avx2
│
├── reports/                  # Results
│   ├── vectorization_report.txt
│   ├── avx2_small.txt
│   
│
├── Makefile                  # Build and run automation
├── README.md                 
└── report.pdf                
```

---


##  Tasks Implemented

### 1. Plain C++ Implementation

A scalar implementation is provided in `src/main.cpp` and compiled into:

* **Baseline version**

  * Auto-vectorization disabled
  * Flags used:

    ```
    -O3 -mavx2 -fno-tree-vectorize
    ```

* **Auto-vectorized version**

  * GCC auto-vectorization enabled
  * Flags used:

    ```
    -O3 -mavx2 -fopt-info-vec-all=reports/vectorization_report.txt
    ```

The compiler report is saved in:

```
reports/vectorization_report.txt
```
I have also verified that compiler sucessfully vectorized the hot loop, the results are available in vectorization_report.txt . 

---

### 2. AVX2 Intrinsics Implementation

A manual SIMD implementation is provided in:

```
src/mapper_avx2.cpp
```

This version uses **AVX2 intrinsics** to process multiple keys in parallel and is compiled into:

```
build/part_avx2
```

The AVX2 version produces **identical output** to the scalar implementation.

---

##  Build Instructions

Run the following commands on the cluster:

```bash
make clean
make all
make autovec
make avx2
```

This generates:

* `build/part_baseline`
* `build/part_autovec`
* `build/part_avx2`

---

##  Running the Code

### 🔹 Baseline & Auto-vectorized

Small test:

```bash
make run-small
```

Large benchmark:

```bash
make run-large
```

---

### 🔹 AVX2 Version

Run both correctness and performance:

```bash
make run-avx2
```

---

##  Correctness Verification

Two methods are used to verify correctness:

### 1. Element-wise Comparison

```bash
--small-check
```

* Compares AVX2 output with scalar reference
* Output:

  ```
  Element-wise self-check: PASS
  ```

---

### 2. Checksum Validation

Each run prints a checksum:

```bash
checksum = XXXXX
```

Matching checksums across implementations confirm identical outputs.

---

##  Testing Platforms

The code was tested on:

* Front-end node of the cluster
* node09 

---

##  Implementation Notes

* Partition count (P) is chosen as a **power of two**

  * Enables fast masking instead of module
* Hash function:

  * Combines upper and lower 32 bits of the key
  * Uses multiplicative hashing
  
* Deterministic input generation using a fixed seed
* AVX2 processes multiple elements per iteration
* Scalar fallback handles remaining elements

---

##  Performance Measurement

* Multiple repetitions (`--repeats`)
* Metrics reported:

  * Median time
  * Mean time
  * Standard deviation
  * Throughput (elements/second)

---

##  Output Files

Results and logs are stored in:

```
reports/
```

Examples:

* `vectorization_report.txt`
* `avx2_small.txt`

---

##  Summary

This project demonstrates:

* Compiler auto-vectorization using GCC
* Manual SIMD optimization using AVX2 intrinsics
* Correctness verification via element-wise comparison and checksums
* Performance evaluation on large datasets

---

