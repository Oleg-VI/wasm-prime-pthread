#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

/// Sieve-of-Eratosthenes prime generator.
///
/// Computation runs in a dedicated std::thread (mapped to a Web Worker
/// via Emscripten pthreads). The browser UI thread is never blocked.
/// No exceptions.
class PrimeGenerator {
public:
    using ProgressCallback = std::function<void(int /*percent: 0-100*/)>;
    using CompleteCallback = std::function<void(std::vector<int> /*primes*/)>;

    PrimeGenerator() noexcept = default;
    ~PrimeGenerator();

    // Non-copyable, non-movable — owns a live thread.
    PrimeGenerator(const PrimeGenerator&)            = delete;
    PrimeGenerator& operator=(const PrimeGenerator&) = delete;

    /// Starts computing all primes below `limit` in a background thread.
    /// @return false if a computation is already in progress.
    [[nodiscard]] bool StartComputation(int limit,
                                        ProgressCallback on_progress,
                                        CompleteCallback on_complete) noexcept;

    /// Requests cancellation. The thread will stop at the next check point.
    void Cancel() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;

private:
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
};