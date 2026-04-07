#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

/*
    Module 1 - Task 1
    Plain C++ implementation of the partition mapping kernel.

    Goal:
    For each 64-bit key, compute a partition id in [0, P).

    Important design choices:
    1. P must be a power of two.
    2. Keys are generated deterministically from a seed.
    3. We use a simple 64-bit mixing function so that the lower bits are well mixed.
    4. partition_id = hash(key) & (P - 1), faster than modulo when P is power of two.

    This SAME source is compiled in two ways:
    - baseline: auto-vectorization disabled
    - autovec: auto-vectorization enabled
*/

// -----------------------------
// Utility: check power of two
// -----------------------------
static inline bool is_power_of_two(uint64_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

// -----------------------------
// Deterministic key generator
// -----------------------------
void generate_keys(std::vector<uint64_t>& keys, uint64_t seed) {
    std::mt19937_64 rng(seed);
    for (size_t i = 0; i < keys.size(); ++i) {
        keys[i] = rng();
    }
}

// Optional duplicate-controlled generator.
// key_space > 0 means keys are drawn from [0, key_space), producing duplicates.
void generate_keys_with_keyspace(std::vector<uint64_t>& keys, uint64_t seed, uint64_t key_space) {
    if (key_space == 0) {
        throw std::invalid_argument("key_space must be > 0");
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist(0, key_space - 1);

    for (size_t i = 0; i < keys.size(); ++i) {
        keys[i] = dist(rng);
    }
}

// -----------------------------




// helper: log2(P) since P is power of two
static inline uint32_t log2_u32(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return 31u - static_cast<uint32_t>(__builtin_clz(x));
#else
    uint32_t r = 0;
    while (x > 1u) {
        x >>= 1u;
        ++r;
    }
    return r;
#endif
}

// -----------------------------
// Hot loop (NEW mapping)
// -----------------------------
void map_keys_to_partitions(const uint64_t* __restrict keys,
                            uint32_t* __restrict part_ids,
                            size_t N,
                            uint32_t P) {

    const uint32_t mask  = P - 1;
    const uint32_t bits  = log2_u32(P);
    const uint32_t shift = 32 - bits;

    const uint32_t C = 0x9E3779B1u; // Knuth constant

    #pragma GCC ivdep
    for (size_t i = 0; i < N; ++i) {
        uint64_t key = keys[i];

        // fold 64-bit -> 32-bit
        uint32_t lo = static_cast<uint32_t>(key);
        uint32_t hi = static_cast<uint32_t>(key >> 32);
        uint32_t x  = lo ^ hi;

        // multiplicative hash
        uint32_t h = x * C;

        // final partition
        part_ids[i] = (h >> shift) & mask;
    }
}


// -----------------------------
// Reference implementation
// Same semantics, kept separate for checking.
// Marked noinline so it does not interfere with hot-loop optimization.
// -----------------------------


void map_keys_to_partitions_reference(const uint64_t* keys,
                                      uint32_t* part_ids,
                                      size_t N,
                                      uint32_t P) {

    const uint32_t mask  = P - 1;
    const uint32_t bits  = log2_u32(P);
    const uint32_t shift = 32 - bits;

    const uint32_t C = 0x9E3779B1u;

    for (size_t i = 0; i < N; ++i) {
        uint64_t key = keys[i];

        uint32_t lo = static_cast<uint32_t>(key);
        uint32_t hi = static_cast<uint32_t>(key >> 32);
        uint32_t x  = lo ^ hi;

        uint32_t h = x * C;

        part_ids[i] = (h >> shift) & mask;
    }
}


// -----------------------------
// Element-wise comparison for small checks
// -----------------------------
bool compare_arrays(const std::vector<uint32_t>& a,
                    const std::vector<uint32_t>& b,
                    bool verbose = false) {
    if (a.size() != b.size()) return false;

    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            if (verbose) {
                std::cerr << "Mismatch at index " << i
                          << ": a=" << a[i]
                          << ", b=" << b[i] << "\n";
            }
            return false;
        }
    }
    return true;
}

// -----------------------------
// Output checksum / hash
// FNV-1a style over partition ids
// -----------------------------
uint64_t checksum_partitions(const uint32_t* part_ids, size_t N) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < N; ++i) {
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

// -----------------------------
// Statistics helpers
// -----------------------------
double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if ((n % 2) == 0) {
        return 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }
    return v[n / 2];
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const double s = std::accumulate(v.begin(), v.end(), 0.0);
    return s / static_cast<double>(v.size());
}

double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double acc = 0.0;
    for (double x : v) {
        const double d = x - m;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(v.size()));
}

// -----------------------------
// Command-line parsing
// -----------------------------
struct Config {
    size_t N = 1 << 20;
    uint32_t P = 256;
    uint64_t seed = 42;
    int repeats = 5;
    bool small_check = false;
    bool print_sample = false;
    bool use_keyspace = false;
    uint64_t key_space = 0;
};

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  --N <value>            Number of keys\n"
        << "  --P <value>            Number of partitions (must be power of two)\n"
        << "  --seed <value>         RNG seed\n"
        << "  --repeats <value>      Benchmark repetitions\n"
        << "  --small-check          Run element-wise reference check\n"
        << "  --print-sample         Print first few outputs\n"
        << "  --key-space <value>    Generate keys in [0, key_space) to create duplicates\n"
        << "  --help                 Show this help\n";
}

Config parse_args(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto need_value = [&](const std::string& name) {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value after " + name);
            }
        };

        if (arg == "--N") {
            need_value(arg);
            cfg.N = std::stoull(argv[++i]);
        } else if (arg == "--P") {
            need_value(arg);
            cfg.P = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed") {
            need_value(arg);
            cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--repeats") {
            need_value(arg);
            cfg.repeats = std::stoi(argv[++i]);
        } else if (arg == "--small-check") {
            cfg.small_check = true;
        } else if (arg == "--print-sample") {
            cfg.print_sample = true;
        } else if (arg == "--key-space") {
            need_value(arg);
            cfg.use_keyspace = true;
            cfg.key_space = std::stoull(argv[++i]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }

    return cfg;
}

// -----------------------------
// Main
// -----------------------------
int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);

        if (!is_power_of_two(cfg.P)) {
            throw std::invalid_argument("P must be a power of two.");
        }

        if (cfg.repeats <= 0) {
            throw std::invalid_argument("repeats must be > 0.");
        }

        std::vector<uint64_t> keys(cfg.N);
        std::vector<uint32_t> part_ids(cfg.N);

        if (cfg.use_keyspace) {
            generate_keys_with_keyspace(keys, cfg.seed, cfg.key_space);
        } else {
            generate_keys(keys, cfg.seed);
        }

        // Optional correctness check
        if (cfg.small_check) {
            std::vector<uint32_t> ref(cfg.N);
            map_keys_to_partitions_reference(keys.data(), ref.data(), cfg.N, cfg.P);
            map_keys_to_partitions(keys.data(), part_ids.data(), cfg.N, cfg.P);

            const bool ok = compare_arrays(ref, part_ids, true);
            std::cout << "Element-wise self-check: " << (ok ? "PASS" : "FAIL") << "\n";
            if (!ok) return 1;
        }

        // Warm-up
        map_keys_to_partitions(keys.data(), part_ids.data(), cfg.N, cfg.P);

        // Benchmark only the kernel
        std::vector<double> times_ms;
        times_ms.reserve(static_cast<size_t>(cfg.repeats));

        for (int r = 0; r < cfg.repeats; ++r) {
            const auto t0 = std::chrono::high_resolution_clock::now();
            map_keys_to_partitions(keys.data(), part_ids.data(), cfg.N, cfg.P);
            const auto t1 = std::chrono::high_resolution_clock::now();

            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            times_ms.push_back(ms);
        }

        const uint64_t chk = checksum_partitions(part_ids.data(), cfg.N);
        const double med_ms = median(times_ms);
        const double avg_ms = mean(times_ms);
        const double sd_ms  = stddev(times_ms);
        const double throughput = (med_ms > 0.0)
            ? (static_cast<double>(cfg.N) / (med_ms / 1000.0))
            : 0.0;

        std::cout << "Configuration:\n";
        std::cout << "  N         = " << cfg.N << "\n";
        std::cout << "  P         = " << cfg.P << "\n";
        std::cout << "  seed      = " << cfg.seed << "\n";
        std::cout << "  repeats   = " << cfg.repeats << "\n";
        if (cfg.use_keyspace) {
            std::cout << "  key_space = " << cfg.key_space << " (duplicates enabled)\n";
        }

        std::cout << "\nResults:\n";
        std::cout << "  checksum              = " << chk << "\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  median time (ms)      = " << med_ms << "\n";
        std::cout << "  mean time (ms)        = " << avg_ms << "\n";
        std::cout << "  stddev time (ms)      = " << sd_ms << "\n";
        std::cout << std::setprecision(2);
        std::cout << "  throughput (elem/s)   = " << throughput << "\n";

        if (cfg.print_sample) {
            const size_t show = std::min<size_t>(10, cfg.N);
            std::cout << "\nSample output:\n";
            for (size_t i = 0; i < show; ++i) {
                std::cout << "i=" << i
                          << " key=" << keys[i]
                          << " part=" << part_ids[i] << "\n";
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
