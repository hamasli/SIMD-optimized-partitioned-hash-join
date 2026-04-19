#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// A record contains only one field: the join key.
struct Record {
    std::uint64_t key{};
};

// Stores the final join result:
// - total number of matches
// - two checksums for correctness verification
struct JoinResult {
    std::uint64_t join_count = 0;
    std::uint64_t checksum1  = 0;
    std::uint64_t checksum2  = 0;
};

// Stores the partitioned relation.
// Partition pid occupies data[begin[pid] .. end[pid]).
struct PartitionedRelation {
    std::vector<Record>      data;
    std::vector<std::size_t> begin;
    std::vector<std::size_t> end;
};

// Stores timing for one partitioning operation.
// For the parallel version, prefix_sec includes:
// - merging local histograms
// - global prefix sum
// - per-thread offset setup
struct PartitionPhaseTimes {
    double histogram_sec = 0.0;
    double prefix_sec    = 0.0;
    double scatter_sec   = 0.0;
    double total_sec     = 0.0;
};

// Stores timing for the full pipeline:
// - partitioning R
// - partitioning S
// - join
// - total
struct PhaseTimes {
    PartitionPhaseTimes r_partition;
    PartitionPhaseTimes s_partition;
    double join_sec  = 0.0;
    double total_sec = 0.0;
};

// Checks whether P is a power of two.
// Our partition mapping assumes this.
static bool is_power_of_two(std::uint32_t x) {
    return x != 0 && (x & (x - 1U)) == 0;
}

// Reads one unsigned integer argument from the command line.
static bool read_arg_u64(int argc, char** argv,
                         const std::string& name,
                         std::uint64_t& out) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            out = std::strtoull(argv[i + 1], nullptr, 10);
            return true;
        }
    }
    return false;
}

// Prints usage if the program is executed with missing parameters.
static void usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog
        << " -nr NR -ns NS -seed SEED -max-key K -p P -t T\n\n"
        << "Parameters:\n"
        << "  -nr         Number of records in relation R\n"
        << "  -ns         Number of records in relation S\n"
        << "  -seed       Deterministic seed\n"
        << "  -max-key    Keys are generated in [0, max-key)\n"
        << "  -p          Number of partitions (must be power of two)\n"
        << "  -t          Number of threads\n";
}

// Internal mixing function used by splitmix64.
// It scrambles bits well and is used for random generation and checksums.
static inline std::uint64_t splitmix64_mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Stateless mixing function used in checksum computation.
static inline std::uint64_t splitmix64(std::uint64_t x) {
    return splitmix64_mix(x + 0x9e3779b97f4a7c15ULL);
}

// Deterministic pseudo-random generator step.
// It updates the state and returns the next value.
static inline std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    return splitmix64_mix(state);
}

// Generates one relation of n records with deterministic keys.
static std::vector<Record> generate_relation(std::size_t n,
                                             std::uint64_t seed,
                                             std::uint64_t max_key) {
    std::vector<Record> out(n);
    std::uint64_t state = seed;

    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r = splitmix64_next(state);
        out[i].key = (max_key == 0) ? 0ULL : (r % max_key);
    }

    return out;
}

// Stores precomputed values for the Module 1 partition mapping.
struct HashParams {
    std::uint32_t shift = 0;
    std::uint32_t mask  = 0;
};

// Computes log2(x) for a power-of-two value x.
static inline std::uint32_t log2_u32(std::uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return 31u - static_cast<std::uint32_t>(__builtin_clz(x));
#else
    std::uint32_t r = 0;
    while (x > 1u) {
        x >>= 1u;
        ++r;
    }
    return r;
#endif
}

// Precomputes the values needed by the partition mapping.
static inline HashParams make_hash_params(std::uint32_t p) {
    const std::uint32_t bits = log2_u32(p);
    return HashParams{
        .shift = 32u - bits,
        .mask  = p - 1u
    };
}

// Module 1 partition mapping:
// 1. fold the 64-bit key into 32 bits using XOR
// 2. multiply by Knuth's constant
// 3. extract the partition id using shift and mask
static inline std::uint32_t compute_partition_id(std::uint64_t key,
                                                 const HashParams& hp) {
    constexpr std::uint32_t C = 0x9E3779B1u;

    const std::uint32_t lo = static_cast<std::uint32_t>(key);
    const std::uint32_t hi = static_cast<std::uint32_t>(key >> 32);
    const std::uint32_t h  = (lo ^ hi) * C;

    return (h >> hp.shift) & hp.mask;
}

// Partitions one relation in parallel.
// The work is divided into three main steps:
// 1. thread-private histograms
// 2. merge + prefix sum + offset setup
// 3. parallel scatter
static PartitionedRelation parallel_partition_relation(
    const std::vector<Record>& rel,
    const HashParams& hp,
    std::uint32_t P,
    int T,
    PartitionPhaseTimes& times)
{
    const std::size_t N = rel.size();
    const int T_eff = std::max(1, std::min<int>(T, static_cast<int>(std::max<std::size_t>(1, N))));

    const auto t0 = std::chrono::steady_clock::now();

    // Each thread builds its own private histogram.
    std::vector<std::vector<std::size_t>> local_hists(
        T_eff, std::vector<std::size_t>(P, 0));

    {
        std::vector<std::thread> threads;
        threads.reserve(T_eff);

        for (int t = 0; t < T_eff; ++t) {
            const std::size_t start = (static_cast<std::size_t>(t) * N) / T_eff;
            const std::size_t stop  = (static_cast<std::size_t>(t + 1) * N) / T_eff;

            threads.emplace_back([&, t, start, stop]() {
                for (std::size_t i = start; i < stop; ++i) {
                    ++local_hists[t][compute_partition_id(rel[i].key, hp)];
                }
            });
        }

        for (auto& th : threads) th.join();
    }

    const auto t1 = std::chrono::steady_clock::now();

    // Merge local histograms into one global histogram.
    std::vector<std::size_t> global_hist(P, 0);
    for (int t = 0; t < T_eff; ++t) {
        for (std::uint32_t pid = 0; pid < P; ++pid) {
            global_hist[pid] += local_hists[t][pid];
        }
    }

    // Compute the start position of each partition.
    std::vector<std::size_t> begin(P, 0);
    {
        std::size_t running = 0;
        for (std::uint32_t pid = 0; pid < P; ++pid) {
            begin[pid] = running;
            running += global_hist[pid];
        }
    }

    // Compute per-thread offsets for lock-free scatter.
    std::vector<std::vector<std::size_t>> thread_offsets(
        T_eff, std::vector<std::size_t>(P, 0));

    for (std::uint32_t pid = 0; pid < P; ++pid) {
        std::size_t offset = begin[pid];
        for (int t = 0; t < T_eff; ++t) {
            thread_offsets[t][pid] = offset;
            offset += local_hists[t][pid];
        }
    }

    const auto t2 = std::chrono::steady_clock::now();

    // Scatter records into the output array using thread-private cursors.
    std::vector<Record> out(N);

    {
        std::vector<std::thread> threads;
        threads.reserve(T_eff);

        for (int t = 0; t < T_eff; ++t) {
            const std::size_t start = (static_cast<std::size_t>(t) * N) / T_eff;
            const std::size_t stop  = (static_cast<std::size_t>(t + 1) * N) / T_eff;

            threads.emplace_back([&, t, start, stop]() {
                std::vector<std::size_t> cursor = thread_offsets[t];

                for (std::size_t i = start; i < stop; ++i) {
                    const std::uint32_t pid = compute_partition_id(rel[i].key, hp);
                    out[cursor[pid]++] = rel[i];
                }
            });
        }

        for (auto& th : threads) th.join();
    }

    const auto t3 = std::chrono::steady_clock::now();

    std::vector<std::size_t> end(P, 0);
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        end[pid] = begin[pid] + global_hist[pid];
    }

    const auto t4 = std::chrono::steady_clock::now();

    times.histogram_sec = std::chrono::duration<double>(t1 - t0).count();
    times.prefix_sec    = std::chrono::duration<double>(t2 - t1).count();
    times.scatter_sec   = std::chrono::duration<double>(t3 - t2).count();
    times.total_sec     = std::chrono::duration<double>(t4 - t0).count();

    return PartitionedRelation{
        .data  = std::move(out),
        .begin = begin,
        .end   = end
    };
}

// Joins one partition of R with the corresponding partition of S.
// Build phase:
//   count how many times each key appears in R
// Probe phase:
//   for each key in S, add the multiplicity found in R
static JoinResult join_one_partition(const PartitionedRelation& Rpart,
                                     const PartitionedRelation& Spart,
                                     std::uint32_t pid) {
    JoinResult result{};

    const std::size_t r_begin = Rpart.begin[pid];
    const std::size_t r_end   = Rpart.end[pid];
    const std::size_t s_begin = Spart.begin[pid];
    const std::size_t s_end   = Spart.end[pid];

    if (r_begin == r_end || s_begin == s_end) {
        return result;
    }

    std::unordered_map<std::uint64_t, std::uint32_t> countR;
    countR.reserve((r_end - r_begin) * 2);

    for (std::size_t i = r_begin; i < r_end; ++i) {
        ++countR[Rpart.data[i].key];
    }

    for (std::size_t i = s_begin; i < s_end; ++i) {
        const std::uint64_t key = Spart.data[i].key;
        const auto it = countR.find(key);

        if (it != countR.end()) {
            const std::uint64_t mult = it->second;
            result.join_count += mult;
            result.checksum1  += splitmix64(key) * mult;
            result.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * mult;
        }
    }

    return result;
}

// Joins all partitions in parallel.
// Each thread gets a range of partition ids.
static JoinResult parallel_join(const PartitionedRelation& Rpart,
                                const PartitionedRelation& Spart,
                                std::uint32_t P,
                                int T) {
    const int T_eff = std::max(1, std::min<int>(T, static_cast<int>(P)));
    std::vector<JoinResult> local_results(T_eff);

    {
        std::vector<std::thread> threads;
        threads.reserve(T_eff);

        for (int t = 0; t < T_eff; ++t) {
            const std::uint32_t p_start =
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(t) * P) / T_eff);
            const std::uint32_t p_stop =
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(t + 1) * P) / T_eff);

            threads.emplace_back([&, t, p_start, p_stop]() {
                JoinResult local{};

                for (std::uint32_t pid = p_start; pid < p_stop; ++pid) {
                    const JoinResult part = join_one_partition(Rpart, Spart, pid);
                    local.join_count += part.join_count;
                    local.checksum1  += part.checksum1;
                    local.checksum2  += part.checksum2;
                }

                local_results[t] = local;
            });
        }

        for (auto& th : threads) th.join();
    }

    JoinResult total{};
    for (const auto& local : local_results) {
        total.join_count += local.join_count;
        total.checksum1  += local.checksum1;
        total.checksum2  += local.checksum2;
    }

    return total;
}

// Runs the full parallel pipeline:
// 1. partition R in parallel
// 2. partition S in parallel
// 3. join partitions in parallel
// 4. accumulate final result
// Also records timing for all main phases.
static JoinResult partitioned_hash_join_parallel(
    const std::vector<Record>& R,
    const std::vector<Record>& S,
    const HashParams& hp,
    std::uint32_t P,
    int T,
    PhaseTimes& times)
{
    const auto t0 = std::chrono::steady_clock::now();
    const PartitionedRelation Rpart = parallel_partition_relation(R, hp, P, T, times.r_partition);

    const PartitionedRelation Spart = parallel_partition_relation(S, hp, P, T, times.s_partition);
    const auto t2 = std::chrono::steady_clock::now();
    const JoinResult total = parallel_join(Rpart, Spart, P, T);
    const auto t3 = std::chrono::steady_clock::now();

    times.join_sec  = std::chrono::duration<double>(t3 - t2).count();
    times.total_sec = std::chrono::duration<double>(t3 - t0).count();

    return total;
}

// Partitions one relation sequentially.
// This is used only as the correctness/performance reference
// inside the parallel binary.
static PartitionedRelation seq_partition_relation(
    const std::vector<Record>& rel,
    const HashParams& hp,
    std::uint32_t P,
    PartitionPhaseTimes& times)
{
    const auto t0 = std::chrono::steady_clock::now();

    std::vector<std::size_t> hist(P, 0);
    for (const auto& rec : rel) {
        ++hist[compute_partition_id(rec.key, hp)];
    }

    const auto t1 = std::chrono::steady_clock::now();

    std::vector<std::size_t> begin(P, 0);
    std::size_t running = 0;
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        begin[pid] = running;
        running += hist[pid];
    }

    const auto t2 = std::chrono::steady_clock::now();

    std::vector<Record> out(rel.size());
    std::vector<std::size_t> next = begin;
    for (const auto& rec : rel) {
        const std::uint32_t pid = compute_partition_id(rec.key, hp);
        out[next[pid]++] = rec;
    }

    const auto t3 = std::chrono::steady_clock::now();

    std::vector<std::size_t> end(P, 0);
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        end[pid] = begin[pid] + hist[pid];
    }

    const auto t4 = std::chrono::steady_clock::now();

    times.histogram_sec = std::chrono::duration<double>(t1 - t0).count();
    times.prefix_sec    = std::chrono::duration<double>(t2 - t1).count();
    times.scatter_sec   = std::chrono::duration<double>(t3 - t2).count();
    times.total_sec     = std::chrono::duration<double>(t4 - t0).count();

    return PartitionedRelation{
        .data  = std::move(out),
        .begin = begin,
        .end   = end
    };
}

// Runs the full sequential pipeline inside this binary.
// This is used for correctness checking and speedup calculation.
static JoinResult partitioned_hash_join_sequential(
    const std::vector<Record>& R,
    const std::vector<Record>& S,
    const HashParams& hp,
    std::uint32_t P,
    PhaseTimes& times)
{
    const auto t0 = std::chrono::steady_clock::now();
    const PartitionedRelation Rpart = seq_partition_relation(R, hp, P, times.r_partition);

    const PartitionedRelation Spart = seq_partition_relation(S, hp, P, times.s_partition);
    const auto t2 = std::chrono::steady_clock::now();

    JoinResult total{};
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const JoinResult local = join_one_partition(Rpart, Spart, pid);
        total.join_count += local.join_count;
        total.checksum1  += local.checksum1;
        total.checksum2  += local.checksum2;
    }

    const auto t3 = std::chrono::steady_clock::now();

    times.join_sec  = std::chrono::duration<double>(t3 - t2).count();
    times.total_sec = std::chrono::duration<double>(t3 - t0).count();

    return total;
}

// Brute-force verifier used only for very small inputs.
static JoinResult naive_join_verifier(const std::vector<Record>& R,
                                      const std::vector<Record>& S) {
    JoinResult result{};

    for (const auto& r : R) {
        for (const auto& s : S) {
            if (r.key == s.key) {
                result.join_count += 1;
                result.checksum1  += splitmix64(r.key);
                result.checksum2  += splitmix64(r.key ^ 0x9e3779b97f4a7c15ULL);
            }
        }
    }

    return result;
}

// Main function:
// - reads arguments
// - generates input relations
// - runs the sequential baseline
// - runs the parallel pipeline
// - prints results and timings
// - verifies correctness
int main(int argc, char** argv) {
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, t = 0;

    if (!read_arg_u64(argc, argv, "-nr",      nr)      ||
        !read_arg_u64(argc, argv, "-ns",      ns)      ||
        !read_arg_u64(argc, argv, "-seed",    seed)    ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p",       p)       ||
        !read_arg_u64(argc, argv, "-t",       t)) {
        usage(argv[0]);
        return 1;
    }

    if (p > std::numeric_limits<std::uint32_t>::max()) {
        std::cerr << "Error: P too large.\n";
        return 1;
    }

    const std::uint32_t P  = static_cast<std::uint32_t>(p);
    const int           T  = static_cast<int>(t);
    const std::size_t   NR = static_cast<std::size_t>(nr);
    const std::size_t   NS = static_cast<std::size_t>(ns);

    if (!is_power_of_two(P)) {
        std::cerr << "Error: P must be a power of two.\n";
        return 1;
    }

    if (T <= 0) {
        std::cerr << "Error: number of threads must be >= 1.\n";
        return 1;
    }

    const HashParams hp = make_hash_params(P);

    const auto R = generate_relation(NR, seed,                          max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    PhaseTimes seq_times{};
    const JoinResult seq_result =
        partitioned_hash_join_sequential(R, S, hp, P, seq_times);

    PhaseTimes par_times{};
    const JoinResult par_result =
        partitioned_hash_join_parallel(R, S, hp, P, T, par_times);

    std::cout << "NR=" << NR << " NS=" << NS
              << " P=" << P << " seed=" << seed
              << " [0, " << max_key << ") threads=" << T << "\n";
    std::cout << "join_count=" << par_result.join_count << "\n";
    std::cout << "checksum1="  << par_result.checksum1  << "\n";
    std::cout << "checksum2="  << par_result.checksum2  << "\n";

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "seq_r_histogram_sec=" << seq_times.r_partition.histogram_sec << "\n";
    std::cout << "seq_r_prefix_sec="    << seq_times.r_partition.prefix_sec    << "\n";
    std::cout << "seq_r_scatter_sec="   << seq_times.r_partition.scatter_sec   << "\n";
    std::cout << "seq_partition_r_sec=" << seq_times.r_partition.total_sec     << "\n";

    std::cout << "seq_s_histogram_sec=" << seq_times.s_partition.histogram_sec << "\n";
    std::cout << "seq_s_prefix_sec="    << seq_times.s_partition.prefix_sec    << "\n";
    std::cout << "seq_s_scatter_sec="   << seq_times.s_partition.scatter_sec   << "\n";
    std::cout << "seq_partition_s_sec=" << seq_times.s_partition.total_sec     << "\n";

    std::cout << "seq_join_sec="        << seq_times.join_sec                  << "\n";
    std::cout << "seq_time_sec="        << seq_times.total_sec                 << "\n";

    std::cout << "par_r_histogram_sec=" << par_times.r_partition.histogram_sec << "\n";
    std::cout << "par_r_prefix_sec="    << par_times.r_partition.prefix_sec    << "\n";
    std::cout << "par_r_scatter_sec="   << par_times.r_partition.scatter_sec   << "\n";
    std::cout << "par_partition_r_sec=" << par_times.r_partition.total_sec     << "\n";

    std::cout << "par_s_histogram_sec=" << par_times.s_partition.histogram_sec << "\n";
    std::cout << "par_s_prefix_sec="    << par_times.s_partition.prefix_sec    << "\n";
    std::cout << "par_s_scatter_sec="   << par_times.s_partition.scatter_sec   << "\n";
    std::cout << "par_partition_s_sec=" << par_times.s_partition.total_sec     << "\n";

    std::cout << "par_join_sec="        << par_times.join_sec                  << "\n";
    std::cout << "par_time_sec="        << par_times.total_sec                 << "\n";

    std::cout << std::setprecision(2);
    std::cout << "speedup=" << (seq_times.total_sec / par_times.total_sec) << "x\n";

    const bool ok =
        par_result.join_count == seq_result.join_count &&
        par_result.checksum1  == seq_result.checksum1  &&
        par_result.checksum2  == seq_result.checksum2;

    std::cout << "par_vs_seq_check=" << (ok ? "PASS" : "FAIL") << "\n";

    if (!ok) {
        std::cerr << "ERROR: parallel and sequential results differ!\n";
        return 1;
    }

    if (NR <= 500 && NS <= 500) {
        const JoinResult naive = naive_join_verifier(R, S);
        const bool naive_ok =
            par_result.join_count == naive.join_count &&
            par_result.checksum1  == naive.checksum1  &&
            par_result.checksum2  == naive.checksum2;

        std::cout << "naive_join_count=" << naive.join_count << "\n";
        std::cout << "naive_checksum1="  << naive.checksum1  << "\n";
        std::cout << "naive_checksum2="  << naive.checksum2  << "\n";
        std::cout << "naive_check="      << (naive_ok ? "PASS" : "FAIL") << "\n";
    }

    return 0;
}
