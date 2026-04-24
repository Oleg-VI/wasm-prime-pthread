#include "prime_generator.h"

#include <algorithm>
#include <cmath>

PrimeGenerator::~PrimeGenerator() {
    Cancel();
    if (thread_.joinable()) thread_.join();
}

bool PrimeGenerator::StartComputation(int limit,
                                       ProgressCallback on_progress,
                                       CompleteCallback on_complete) noexcept {
    if (running_.load()) return false;

    // Join the previous (already finished) thread before replacing it.
    if (thread_.joinable()) thread_.join();

    if (limit < 2) {
        on_progress(100);
        on_complete({});
        return true;
    }

    cancelled_.store(false, std::memory_order_relaxed);
    running_.store(true,    std::memory_order_relaxed);

    thread_ = std::thread([this, limit,
                           on_progress = std::move(on_progress),
                           on_complete = std::move(on_complete)]()
    {
        const int sqrt_limit = static_cast<int>(
            std::sqrt(static_cast<double>(limit)));
        const int range = std::max(1, sqrt_limit - 2);

        std::vector<bool> sieve(static_cast<std::size_t>(limit + 1), true);
        sieve[0] = sieve[1] = false;

        int last_reported = -1;

        for (int f = 2; f <= sqrt_limit; ++f) {
            if (cancelled_.load(std::memory_order_relaxed)) {
                running_.store(false);
                return;
            }

            if (sieve[static_cast<std::size_t>(f)]) {
                for (long long m = static_cast<long long>(f) * f;
                     m <= limit; m += f) {
                    sieve[static_cast<std::size_t>(m)] = false;
                }
            }

            // Throttle to 100 JS calls max (one per 1% step).
            const int progress = std::min(99,
                static_cast<int>(100LL * (f - 2) / range));
            if (progress != last_reported) {
                last_reported = progress;
                on_progress(progress);
            }
        }

        // Check cancel before the expensive collect + copy phase.
        if (cancelled_.load(std::memory_order_relaxed)) {
            running_.store(false);
            return;
        }

        // Collect results.
        const auto total = static_cast<std::size_t>(
            std::count(sieve.cbegin(), sieve.cend(), true));

        std::vector<int> results;
        results.reserve(total);
        for (int i = 2; i <= limit; ++i) {
            if (sieve[static_cast<std::size_t>(i)]) {
                results.push_back(i);
            }
        }

        on_progress(100);
        on_complete(std::move(results));
        running_.store(false);
    });

    return true;
}

void PrimeGenerator::Cancel() noexcept {
    cancelled_.store(true, std::memory_order_relaxed);
}

bool PrimeGenerator::IsRunning() const noexcept {
    return running_.load();
}
