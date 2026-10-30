// In-process benchmark of the storage engine (no network): measures per-operation latency
// for the core commands and reports throughput and latency percentiles.
//
// Usage: memkv-bench [operations]   (default 1,000,000)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "memkv/common/clock.hpp"
#include "memkv/storage/database.hpp"

namespace {

using memkv::storage::Database;
using memkv::storage::ScoredMember;
using Nanos = std::chrono::nanoseconds;

struct Stats {
    double ops_per_second;
    double p50_ns;
    double p99_ns;
    double p999_ns;
};

/// Times each call of `op(i)` for i in [0, count) individually.
template <typename Op>
Stats measure(std::size_t count, Op&& op) {
    std::vector<std::int64_t> samples(count);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto before = std::chrono::steady_clock::now();
        op(i);
        samples[i] =
            std::chrono::duration_cast<Nanos>(std::chrono::steady_clock::now() - before).count();
    }
    const double elapsed_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::ranges::sort(samples);
    const auto percentile = [&samples](double p) {
        const auto index = static_cast<std::size_t>(p * static_cast<double>(samples.size() - 1));
        return static_cast<double>(samples[index]);
    };
    return {static_cast<double>(count) / elapsed_s, percentile(0.50), percentile(0.99),
            percentile(0.999)};
}

void report(const char* name, const Stats& stats) {
    std::printf("%-34s %12.0f %10.0f %10.0f %10.0f\n", name, stats.ops_per_second, stats.p50_ns,
                stats.p99_ns, stats.p999_ns);
}

std::vector<std::string> make_keys(std::size_t count, const char* prefix) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back(prefix + std::to_string(i));
    }
    return keys;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const memkv::SteadyClock clock;
    std::mt19937_64 rng(12345);

    const std::vector<std::string> keys = make_keys(n, "key:");
    const std::vector<std::string> members = make_keys(n, "member:");
    std::vector<std::size_t> shuffled(n);
    for (std::size_t i = 0; i < n; ++i) {
        shuffled[i] = i;
    }
    std::ranges::shuffle(shuffled, rng);
    std::uniform_real_distribution<double> score_dist(0.0, 1'000'000.0);
    std::vector<double> scores(n);
    for (double& score : scores) {
        score = score_dist(rng);
    }

    std::printf("memkv storage benchmark, %zu operations per row (latency in ns)\n\n", n);
    std::printf("%-34s %12s %10s %10s %10s\n", "operation", "ops/sec", "p50", "p99", "p99.9");

    {
        Database db(clock);
        report("SET (new keys)", measure(n, [&](std::size_t i) { db.set(keys[i], "value"); }));
        report("GET (hit, random order)",
               measure(n, [&](std::size_t i) { (void)db.get(keys[shuffled[i]]); }));
        report("SET (overwrite)",
               measure(n, [&](std::size_t i) { db.set(keys[shuffled[i]], "other"); }));
        report("SET EX (with TTL)", measure(n, [&](std::size_t i) {
                   db.set(keys[i], "value", {.ttl = std::chrono::seconds(3600)});
               }));
        report("DEL", measure(n, [&](std::size_t i) { db.del(keys[shuffled[i]]); }));
    }
    {
        Database db(clock, {.max_keys = n / 10});
        report("SET with LRU eviction (cap N/10)",
               measure(n, [&](std::size_t i) { db.set(keys[i], "value"); }));
    }
    {
        Database db(clock);
        report("ZADD (1 set, N members)", measure(n, [&](std::size_t i) {
                   const ScoredMember member{members[i], scores[i]};
                   db.zadd("leaderboard", {&member, 1});
               }));
        report("ZSCORE", measure(n, [&](std::size_t i) {
                   (void)db.zscore("leaderboard", members[shuffled[i]]);
               }));
        report("ZRANK", measure(n, [&](std::size_t i) {
                   (void)db.zrank("leaderboard", members[shuffled[i]]);
               }));
        report("ZRANGE (10 by rank)", measure(n, [&](std::size_t i) {
                   const auto start = static_cast<std::int64_t>(shuffled[i] % (n - 10));
                   (void)db.zrange("leaderboard", start, start + 9);
               }));
        report("ZRANGEBYSCORE (~10 by score)", measure(n, [&](std::size_t i) {
                   const double min = scores[shuffled[i]];
                   const double width = 10.0 * 1'000'000.0 / static_cast<double>(n);
                   (void)db.zrange_by_score("leaderboard", {min, false}, {min + width, false});
               }));
    }
    return 0;
}
