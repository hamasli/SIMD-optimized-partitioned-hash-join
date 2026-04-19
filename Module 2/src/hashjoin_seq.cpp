#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
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

// Stores timing for one partitioning operation.
// This helps us see how much time is spent in:
//  histogram,prefix sum ,scatter,total partitioning
struct PartitionPhaseTimes {
    double histogram_sec = 0.0;
    double prefix_sec    = 0.0;
    double scatter_sec   = 0.0;
    double total_sec     = 0.0;
};

// Stores timing for the full sequential pipeline:
// - partitioning R ,partitioning S, join ,full total
struct SeqPhaseTimes {
    PartitionPhaseTimes r_partition;
    PartitionPhaseTimes s_partition;
    double join_sec  = 0.0;
    double total_sec = 0.0;
};

// Checks whether P is a power of two. Our partition mapping assumes this.
static bool is_power_of_two(std::uint32_t x) {
    return x != 0 && (x & (x - 1U)) == 0;
}

// Reads one unsigned integer argument from the command line.
// Example: -nr 1000
static bool read_arg_u64(int argc, char** argv, const std::string& name, std::uint64_t& out) {
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
        << "  " << prog << " -nr NR -ns NS -seed SEED -max-key K -p P\n\n"
        << "Parameters:\n"
        << "  -nr         Number of records in relation R\n"
        << "  -ns         Number of records in relation S\n"
        << "  -seed       Deterministic seed\n"
        << "  -max-key    Keys are generated in [0, max-key)\n"
        << "  -p          Number of partitions (must be power of two)\n";
}

// Internal mixing function used by splitmix64.
// It scrambles bits well and is used for random generation and checksums.
static inline std::uint64_t splitmix64_mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

// Stateless mixing function used in checksum computation.
// Same input always gives the same output.
static inline std::uint64_t splitmix64(std::uint64_t x) {
    return splitmix64_mix(x + 0x9e3779b97f4a7c15ULL);
}

// Deterministic pseudo-random generator step.
// It updates the state and returns the next value.
static inline std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    return splitmix64_mix(state);
}

// Generates one relation of n records.
// Keys are deterministic because the generator uses a fixed seed.
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
// This avoids recomputing shift and mask for every key.
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
    const std::uint32_t x  = lo ^ hi;
    const std::uint32_t h  = x * C;

    return (h >> hp.shift) & hp.mask;
}

// Builds the histogram for one relation.
// hist[pid] = number of records that go to partition pid
static std::vector<std::size_t> compute_histogram(const std::vector<Record>& rel,
                                                  const HashParams& hp,
                                                  std::uint32_t p) {
    std::vector<std::size_t> hist(p, 0);

    for (const auto& rec : rel) {
        const std::uint32_t pid = compute_partition_id(rec.key, hp);
        ++hist[pid];
    }

    return hist;
}

// Computes the exclusive prefix sum of the histogram.
// This gives the starting position of each partition in the output array.
static std::vector<std::size_t> exclusive_prefix_sum(const std::vector<std::size_t>& hist) {
    std::vector<std::size_t> begin(hist.size(), 0);

    std::size_t running = 0;
    for (std::size_t pid = 0; pid < hist.size(); ++pid) {
        begin[pid] = running;
        running += hist[pid];
    }

    return begin;
}

// Rearranges records so that all records of the same partition
// are stored contiguously in memory.
static std::vector<Record> scatter_partitioned(const std::vector<Record>& rel,
                                               const HashParams& hp,
                                               const std::vector<std::size_t>& begin) {
    std::vector<Record> out(rel.size());
    std::vector<std::size_t> next = begin;

    for (const auto& rec : rel) {
        const std::uint32_t pid = compute_partition_id(rec.key, hp);
        out[next[pid]++] = rec;
    }

    return out;
}

// Stores the fully partitioned relation.
// Partition pid occupies data[begin[pid] .. end[pid]).
struct PartitionedRelation {
    std::vector<Record>      data;
    std::vector<std::size_t> begin;
    std::vector<std::size_t> end;
};

// Runs the full partitioning pipeline for one relation:
// histogram -> prefix sum -> scatter -> end offsets
// Also records the timing of each partitioning sub-phase.
static PartitionedRelation partition_relation(const std::vector<Record>& rel,
                                             const HashParams& hp,
                                             std::uint32_t p,
                                             PartitionPhaseTimes& times) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto hist = compute_histogram(rel, hp, p);
    const auto t1 = std::chrono::steady_clock::now();

    const auto begin = exclusive_prefix_sum(hist);
    const auto t2 = std::chrono::steady_clock::now();

    auto data = scatter_partitioned(rel, hp, begin);
    const auto t3 = std::chrono::steady_clock::now();

    std::vector<std::size_t> end(p, 0);
    for (std::uint32_t pid = 0; pid < p; ++pid) {
        end[pid] = begin[pid] + hist[pid];
    }
    const auto t4 = std::chrono::steady_clock::now();

    times.histogram_sec = std::chrono::duration<double>(t1 - t0).count();
    times.prefix_sec    = std::chrono::duration<double>(t2 - t1).count();
    times.scatter_sec   = std::chrono::duration<double>(t3 - t2).count();
    times.total_sec     = std::chrono::duration<double>(t4 - t0).count();

    return PartitionedRelation{
        .data  = std::move(data),
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
            const std::uint64_t multiplicity = it->second;
            result.join_count += multiplicity;
            result.checksum1  += splitmix64(key) * multiplicity;
            result.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * multiplicity;
        }
    }

    return result;
}

// Runs the full sequential pipeline:
// 1. partition R
// 2. partition S
// 3. join all matching partitions
// 4. accumulate final result
// Also records timing for partitioning and join.
static JoinResult partitioned_hash_join_sequential(const std::vector<Record>& R,
                                                   const std::vector<Record>& S,
                                                   std::uint32_t p,
                                                   SeqPhaseTimes& times) {
    const HashParams hp = make_hash_params(p);

    const auto t0 = std::chrono::steady_clock::now();
    const PartitionedRelation Rpart = partition_relation(R, hp, p, times.r_partition);
    
    const PartitionedRelation Spart = partition_relation(S, hp, p, times.s_partition);
    const auto t2 = std::chrono::steady_clock::now();

    JoinResult total{};
    for (std::uint32_t pid = 0; pid < p; ++pid) {
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
// It compares every record in R with every record in S.
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
// - runs the sequential join
// - prints results and timings
// - performs naive correctness verification for tiny inputs
int main(int argc, char** argv) {
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;

    if (!read_arg_u64(argc, argv, "-nr",      nr)      ||
        !read_arg_u64(argc, argv, "-ns",      ns)      ||
        !read_arg_u64(argc, argv, "-seed",    seed)    ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p",       p)) {
        usage(argv[0]);
        return 1;
    }

    if (p > std::numeric_limits<std::uint32_t>::max()) {
        std::cerr << "Error: P too large.\n";
        return 1;
    }

    const std::uint32_t P = static_cast<std::uint32_t>(p);

    if (!is_power_of_two(P)) {
        std::cerr << "Error: P must be a power of two.\n";
        return 1;
    }

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);

    const auto R = generate_relation(NR, seed,                          max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    SeqPhaseTimes times{};
    const JoinResult result = partitioned_hash_join_sequential(R, S, P, times);

    std::cout << "NR=" << NR << " NS=" << NS << " P=" << P
              << " seed=" << seed
              << " [0, " << max_key << ")\n";
    std::cout << "join_count=" << result.join_count << "\n";
    std::cout << "checksum1="  << result.checksum1  << "\n";
    std::cout << "checksum2="  << result.checksum2  << "\n";

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "r_histogram_sec=" << times.r_partition.histogram_sec << "\n";
    std::cout << "r_prefix_sec="    << times.r_partition.prefix_sec    << "\n";
    std::cout << "r_scatter_sec="   << times.r_partition.scatter_sec   << "\n";
    std::cout << "partition_r_sec=" << times.r_partition.total_sec     << "\n";

    std::cout << "s_histogram_sec=" << times.s_partition.histogram_sec << "\n";
    std::cout << "s_prefix_sec="    << times.s_partition.prefix_sec    << "\n";
    std::cout << "s_scatter_sec="   << times.s_partition.scatter_sec   << "\n";
    std::cout << "partition_s_sec=" << times.s_partition.total_sec     << "\n";

    std::cout << "join_sec="        << times.join_sec                  << "\n";
    std::cout << "time_sec="        << times.total_sec                 << "\n";

    if (NR <= 500 && NS <= 500) {
        const JoinResult naive = naive_join_verifier(R, S);
        const bool ok = (result.join_count == naive.join_count &&
                         result.checksum1  == naive.checksum1  &&
                         result.checksum2  == naive.checksum2);

        std::cout << "naive_join_count=" << naive.join_count << "\n";
        std::cout << "naive_checksum1="  << naive.checksum1  << "\n";
        std::cout << "naive_checksum2="  << naive.checksum2  << "\n";
        std::cout << "correctness_check=" << (ok ? "PASS" : "FAIL") << "\n";
    }

    return 0;
}
